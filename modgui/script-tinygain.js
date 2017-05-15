function (event) {

    function to_db (value) {
        if (value < 0.000001) {
            return "-inf";
        }
        return (20 * Math.log10 (value)).toFixed(1);
    }

    function highlight (c) {
        var dpm = event.icon.find ('[mod-role=dpm]');
        var old = dpm.data ('xBgColor');
        if (old == c) {
            return;
        }
        switch (c) {
            case 3:
                dpm.css({backgroundColor: '#ff4400'});
                break;
            case 2:
                dpm.css({backgroundColor: '#dd6622'});
                break;
            case 1:
                dpm.css({backgroundColor: '#ccaa66'});
                break;
            default:
                dpm.css({backgroundColor: '#aacc66'});
                break;
        }
        dpm.data ('xBgColor', c);
		}

    function handle_event (symbol, value) {
        switch (symbol) {
            case 'level':
                var db = to_db(value);
                event.icon.find ('[mod-role=level]').text (db);
                if (db < -3 || value < 0.000001) {
                    highlight (0);
                } else if (db < -1) {
                    highlight (1);
                } else if (db < 0) {
                    highlight (2);
                } else {
                    highlight (3);
                }
                break;
            default:
                break;
        }
    }

    if (event.type == 'start') {
        var dpm = event.icon.find ('[mod-role=dpm]');
        dpm.data ('xBgColor', 0);
        var ports = event.ports;
        for (var p in ports) {
            handle_event (ports[p].symbol, ports[p].value);
        }
    }
    else if (event.type == 'change') {
        handle_event (event.symbol, event.value);
    }
}
