#ifndef HTML_H
#define HTML_H
#include <pgmspace.h>

static PGM_P status_html PROGMEM  = R"Q(
<!doctype html>
<html lang="en">

<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1, shrink-to-fit=no">
    <link rel="stylesheet" href="https://unpkg.com/spectre.css/dist/spectre.min.css">
    <link rel="stylesheet" href="https://unpkg.com/spectre.css/dist/spectre-exp.min.css">
    <link rel="stylesheet" href="https://unpkg.com/spectre.css/dist/spectre-icons.min.css">
    <script src="https://cdn.jsdelivr.net/npm/jquery@3.4.1/dist/jquery.min.js"></script>
    <script src="/code.js"></script>
    <title>NightLight</title>
</head>

<body>

    <div class="container grid-xl">
        <header class="navbar">
            <section class="navbar-section">
                <a href="/index.html" class="navbar-brand mr-2">NightLight</a>
                <a href="/status.html" class="btn btn-link">Status</a>
            </section>
        </header>

        <h1>Sensors</h1>
        <table class="table">
            <thead>
                <tr>
                    <th>Sensor</th>
                    <th>Value</th>
                </tr>
            </thead>
            <tbody id="liveRows">
            </tbody>
        </table>
        <h1>Status</h1>
        <table class="table">
            <thead>
                <tr>
                    <th>Item</th>
                    <th>Status</th>
                </tr>
            </thead>
            <tbody id="statusRows">
            </tbody>
        </table>

    </div>

    <script>
        $(function () {
            function updateSensors() {
                $.getJSON("/sensors.json", function (data) {
                    $('#liveRows').empty();
                    data.forEach(e => {
                        let name = htmlEscape(e.name);
                        let value = htmlEscape(e.value);
                        $('#liveRows').append("<tr><td>" + name + "</td><td>" + value + "</td></tr>");
                    });
                }).fail(e => {
                    console.log("Failed", e);
                })
            }
            function updateStatus() {
                $.getJSON("/status.json", function (data) {
                    $('#statusRows').empty();
                    data.forEach(e => {
                        let name = htmlEscape(e.name);
                        let value = htmlEscape(e.value);
                        $('#statusRows').append("<tr><td>" + name + "</td><td>" + value + "</td></tr>");
                    });
                }).fail(e => {
                    console.log("Failed", e);
                })
            }

            setInterval($.proxy(updateSensors), 1000);
            setInterval($.proxy(updateStatus), 5000);
        });
    </script>
</body>

</html>
)Q";

static PGM_P index_html PROGMEM  = R"Q(
<!doctype html>
<html lang="en">

<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1, shrink-to-fit=no">
    <link rel="stylesheet" href="https://unpkg.com/spectre.css/dist/spectre.min.css">
    <link rel="stylesheet" href="https://unpkg.com/spectre.css/dist/spectre-exp.min.css">
    <link rel="stylesheet" href="https://unpkg.com/spectre.css/dist/spectre-icons.min.css">
    <script src="https://cdn.jsdelivr.net/npm/jquery@3.4.1/dist/jquery.min.js"></script>
    <script src="/code.js"></script>
    <title>NightLight</title>
</head>

<body>

    <div class="container grid-xl">
        <header class="navbar">
            <section class="navbar-section">
                <a href="/index.html" class="navbar-brand mr-2">NightLight</a>
                <a href="/status.html" class="btn btn-link">Status</a>
            </section>
        </header>

        <h1>Alarms</h1>
        <form id="form" class="form-horizontal">
        </form>
    </div>

    <script>
        let weekday = new Array(7);
        weekday[0] = "Sunday";
        weekday[1] = "Monday";
        weekday[2] = "Tuesday";
        weekday[3] = "Wednesday";
        weekday[4] = "Thursday";
        weekday[5] = "Friday";
        weekday[6] = "Saturday";
        let settings;

        function setState(idx, element) {
            settings[idx].enabled = element.checked;
            $.post("/set", JSON.stringify(settings));
        }

        function setTime(idx, element) {
            let hour = parseInt(element.value[0]) * 10 + parseInt(element.value[1]);
            let minute = parseInt(element.value[3]) * 10 + parseInt(element.value[4]);
            settings[idx].second = hour * 60 * 60 + minute * 60;
            $.post("/set", JSON.stringify(settings));
        }

        $.getJSON("/settings.json", data => {
            settings = data;
            let form = $('#form');
            data.forEach((e, idx) => {
                if (idx > 6) return;
                let hour = Math.floor(e.second / 60 / 60);
                if (hour < 10) hour = "0" + hour;
                let minute = Math.floor(e.second / 60 % 60);
                if (minute < 10) minute = "0" + minute;
                form.append(html`
                <div class="form-group">
                    <div class="col-2">
                        <label class="form-switch form-inline">
                            <input type="checkbox" ${e.enabled ? "checked" : ""} onchange="setState(${idx}, this);">
                            <i class="form-icon"></i> ${weekday[e.dow]}
                        </label>
                    </div>
                    <div class="col-10">
                        <label class="form-inline">
                            <input class="form-input" type="time" value="${hour}:${minute}" onchange="setTime(${idx}, this);">
                        </label>
                    </div>
                </div>
                `);
            });
        });
    </script>
</body>

</html>
)Q";

static PGM_P code_js PROGMEM  = R"Q(
// List of the characters we want to escape and their HTML escaped version

const chars = {
    "&": "&amp;",
    ">": "&gt;",
    "<": "&lt;",
    '"': "&quot;",
    "'": "&#39;",
    "`": "&#96;"
};

// Dynamically create a RegExp from the `chars` object
const re = new RegExp(Object.keys(chars).join("|"), "g");

// Return the escaped string
function htmlEscape(str = "") {
    return String(str).replace(re, match => chars[match]);
}

function html(literals, ...substs) {
    return literals.raw.reduce((acc, lit, i) => {
        let subst = substs[i - 1];
        if (Array.isArray(subst)) {
            subst = subst.join("");
        } else if (literals.raw[i - 1] && literals.raw[i - 1].endsWith("$")) {
            // If the interpolation is preceded by a dollar sign,
            // substitution is considered safe and will not be escaped
            acc = acc.slice(0, -1);
        } else {
            subst = htmlEscape(subst);
        }

        return acc + subst + lit;
    });
}
)Q";
#endif
