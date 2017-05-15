/* MOD gain + meter
 *
 * Copyright (C) 2016,2017 Robin Gareus <robin@gareus.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#define _GNU_SOURCE

#define TINYGAIN_URI "http://gareus.org/oss/lv2/tinygain"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#include <lv2/lv2plug.in/ns/lv2core/lv2.h>

#define THROTTLE // MOD output port event throttle
#define SEPARATE_LOOPS // vectorizable

enum {
	P_GAIN = 0,
	P_ENABLE,
	P_LEVEL,
	P_AIN,
	P_AOUT,
	P_AIN2,
	P_AOUT2,
	P_LAST,
};

typedef struct {
	/* ports */
	float* ports[P_LAST];

	/* internal state */
	float meter_level;
	float gain;
	float target_gain;
	float target_gain_db;

#ifdef THROTTLE
	float db_lvl;
#endif

	/* config */
	float    rate;
	float    omega;
	float    falloff;
	uint32_t spp;
} TinyGain;


/* *****************************************************************************
 * LV2 Plugin
 */

static LV2_Handle
instantiate (const LV2_Descriptor*     descriptor,
             double                    rate,
             const char*               bundle_path,
             const LV2_Feature* const* features)
{
	TinyGain* self = (TinyGain*)calloc (1, sizeof (TinyGain));

	self->rate = rate;
  self->omega = 9.72 / rate;
  self->target_gain = 1.0;
	return (LV2_Handle)self;
}

static void
connect_port (LV2_Handle instance,
              uint32_t   port,
              void*      data)
{
	TinyGain* self = (TinyGain*)instance;
	if (port < P_LAST) {
		self->ports[port] = (float*)data;
	}
}

static inline float
fast_fabsf (float val)
{
	union {float f; int i;} t;
	t.f = val;
	t.i &= 0x7fffffff;
	return t.f;
}

static inline float
fast_log2 (float val)
{
	union {float f; int i;} t;
	t.f = val;
	int* const    exp_ptr =  &t.i;
	int           x = *exp_ptr;
	const int     log_2 = ((x >> 23) & 255) - 128;
	x &= ~(255 << 23);
	x += 127 << 23;
	*exp_ptr = x;
	val = ((-1.0f / 3.f) * t.f + 2) * t.f - 2.0f / 3.f;
	return (val + log_2);
}

static inline float
fast_log10f (const float val)
{
  return fast_log2(val) / 3.312500f;
}


static inline void
pre (TinyGain* self, uint32_t n_samples)
{
	if (self->spp != n_samples) {
		const float fall = 15.0f;
		const float tme  = (float) n_samples / self->rate;
		self->falloff    = powf (10.0f, -0.05f * fall * tme);
		self->omega      = 1.f - expf (-2.f * M_PI * 20.f / self->rate);
		self->spp = n_samples;
	}

	if (self->target_gain_db != *self->ports[P_GAIN]) {
		self->target_gain_db = *self->ports[P_GAIN];
		float gain = self->target_gain_db;
		if (gain < -20) gain = -20;
		if (gain >  20) gain =  20;
		self->target_gain = powf (10.0f, 0.05f * gain);
	}
}

static inline void
post (TinyGain* self)
{
	const float l = self->meter_level;
#ifdef THROTTLE
	float db_lvl = l > 1e-6f  ? 20.f * fast_log10f (l) : -120;
	if (db_lvl == -120 && self->db_lvl != -120) {
		*self->ports[P_LEVEL] = 0;
		self->db_lvl = db_lvl;
	}
	else if (fast_fabsf (db_lvl - self->db_lvl) > .2) {
		*self->ports[P_LEVEL] = l;
		self->db_lvl = db_lvl;
	}
#else
	*self->ports[P_LEVEL] = l;
#endif
}

static void
run_mono (LV2_Handle instance, uint32_t n_samples)
{
	TinyGain* self = (TinyGain*)instance;
	pre (self, n_samples);

	float l = self->meter_level + 1e-20f;
	float g = self->gain + 1e-10;
	float t = self->target_gain;

	const float omega = self->omega;
	const float* in   = self->ports[P_AIN];
	float*       out  = self->ports[P_AOUT];

	l *= self->falloff;

	if (*self->ports[P_ENABLE] <= 0) {
		t = 1.0;
	}

#ifdef SEPARATE_LOOPS
	// vectorizable loop
	for (uint32_t i = 0; i < n_samples; ++i) {
		g += omega * (t - g);
		out[i] = in[i] * g;
	}
	// branches inside loop
	for (uint32_t i = 0; i < n_samples; ++i) {
		const float a = fast_fabsf (out[i]);
		if (a > l) { l = a; }
	}
#else
	for (uint32_t i = 0; i < n_samples; ++i) {
		g += omega * (t - g);
		out[i] = in[i] * g;
		const float a = fast_fabsf (out[i]);
		if (a > l) { l = a; }
	}
#endif

	if (!isfinite (l)) l = 0;
	self->meter_level = l;
	self->gain = g;

	post (self);
}

static void
run_stereo (LV2_Handle instance, uint32_t n_samples)
{
	TinyGain* self = (TinyGain*)instance;
	pre (self, n_samples);

	float l = self->meter_level + 1e-20f;
	float g = self->gain + 1e-10;
	float t = self->target_gain;

	const float omega  = self->omega;
	const float* inL   = self->ports[P_AIN];
	const float* inR   = self->ports[P_AIN2];
	float*       outL  = self->ports[P_AOUT];
	float*       outR  = self->ports[P_AOUT2];

	l *= self->falloff;

	if (*self->ports[P_ENABLE] <= 0) {
		t = 1.0;
	}

#ifdef SEPARATE_LOOPS
	// vectorizable loop
	for (uint32_t i = 0; i < n_samples; ++i) {
		g += omega * (t - g);
		outL[i] = inL[i] * g;
		outR[i] = inR[i] * g;
	}
	// branches inside loop
	for (uint32_t i = 0; i < n_samples; ++i) {
		const float aL = fast_fabsf (outL[i]);
		const float aR = fast_fabsf (outR[i]);
		if (aL > l) { l = aL; }
		if (aR > l) { l = aR; }
	}
#else
	for (uint32_t i = 0; i < n_samples; ++i) {
		g += omega * (t - g);
		outL[i] = inL[i] * g;
		outR[i] = inR[i] * g;
		const float aL = fast_fabsf (outL[i]);
		const float aR = fast_fabsf (outR[i]);
		if (aL > l) { l = aL; }
		if (aR > l) { l = aR; }
	}
#endif

	if (!isfinite (l)) l = 0;
	self->meter_level = l;
	self->gain = g;

	post (self);
}

static void
cleanup (LV2_Handle instance)
{
	free (instance);
}

static const void*
extension_data (const char* uri)
{
	return NULL;
}

static const LV2_Descriptor descriptor_mono = {
	TINYGAIN_URI "#mono",
	instantiate,
	connect_port,
	NULL,
	run_mono,
	NULL,
	cleanup,
	extension_data
};

static const LV2_Descriptor descriptor_stereo = {
	TINYGAIN_URI "#stereo",
	instantiate,
	connect_port,
	NULL,
	run_stereo,
	NULL,
	cleanup,
	extension_data
};


#undef LV2_SYMBOL_EXPORT
#ifdef _WIN32
#    define LV2_SYMBOL_EXPORT __declspec(dllexport)
#else
#    define LV2_SYMBOL_EXPORT  __attribute__ ((visibility ("default")))
#endif
LV2_SYMBOL_EXPORT
const LV2_Descriptor*
lv2_descriptor (uint32_t index)
{
	switch (index) {
	case 0:
		return &descriptor_mono;
	case 1:
		return &descriptor_stereo;
	default:
		return NULL;
	}
}
