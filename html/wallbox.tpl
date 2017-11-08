<!doctype html>
<html>
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Wallbox Configuration</title>
<link rel="stylesheet" type="text/css" href="style.css"/>
<script type="text/javascript" src="js/140medley.min.js"></script>
<script type="text/javascript">
    var xhr=j();
    var currWallbox = "%Wallbox%";
    var currUriBase = "%UriBase%";
    var currSelections;

    function nextLetter(letter) {
        if (letter == "H" || letter == "N") {
            return String.fromCharCode(letter.charCodeAt(0) + 2);
        } else {
            return String.fromCharCode(letter.charCodeAt(0) + 1);
        }
    }

    function nextColumnLetter(letter) {
        var letterString = "ABCDEFGHJKLMNPQRSTUV";
        var index = letterString.indexOf(letter);
        if (index < 0 || index > 9) {
            return "";
        }

        return letterString[index + 10];
    }

    function buildSongSheet() {
        var x = document.getElementById("song_sheet");
        x.innerHTML = "";

        var pages = 0;
        var pageLayout = 0;

        var w = document.getElementById("wallbox_type");
        var wallboxType = w.options[w.selectedIndex].value;
        if (wallboxType == "SEEBURG_3W1_100") {
            pages = 5;
            pageLayout = 1;
        } else if(wallboxType == "SEEBURG_V3WA_200") {
            pages = 10;
            pageLayout = 2;
        }

        if (pages == 0 || pageLayout == 0) {
            document.getElementById("base_path").style.display = "none";
            document.getElementById("top_buttons").style.display = "none";
            document.getElementById("bottom_buttons").style.display = "none";
            return;
        }

        var letter = "A";
        var number = 1;
        for (var p = 0; p < pages; p++) {
            var tbl = document.createElement("table");
            tbl.border="1";
            tbl.cellPadding="5";
            var header = tbl.createTHead();
            var row = header.insertRow(0);
            var th = document.createElement("th");
            th.innerHTML = "Code";
            row.appendChild(th);
            th = document.createElement("th");
            th.innerHTML = "Song";
            row.appendChild(th);
            th = document.createElement("th");
            th.innerHTML = "Code";
            row.appendChild(th);
            th = document.createElement("th");
            th.innerHTML = "Song";
            row.appendChild(th);

            var body = tbl.createTBody();
            for (var q = 0; q < 10; q++) {
                if (pageLayout == 1) {
                    number = q + 1;
                } else if (pageLayout == 2) {
                    number = p + 1;
                }
                
                row = body.insertRow(-1);

                var code = letter + number;
                var td = document.createElement("td");
                td.innerHTML = code;
                td.style.textAlign = "center";
                td.style.color = "white";
                td.style.backgroundColor = "#e33c72";
                row.appendChild(td);

                td = document.createElement("td");
                td.style.backgroundColor = "#b1d7ae";

                var input = document.createElement("input");
                input.type = "text";
                input.name = "song-" + code;
                input.size = 15;
                input.maxlength = 15;
                if (currSelections.hasOwnProperty(code)) {
                    input.value = currSelections[code];
                }
                input.addEventListener('change', function(evt) {
                    if (evt.target.name.indexOf("song-") == 0) {
                        var code = evt.target.name.substring(5);
                        currSelections[code] = evt.target.value;
                    }
                });
                td.appendChild(input);

                row.appendChild(td);

                if (pageLayout == 1) {
                    code = nextLetter(letter) + number;
                } else if (pageLayout == 2) {
                    code = nextColumnLetter(letter) + number;
                }
                td = document.createElement("td");
                td.innerHTML = code;
                td.style.textAlign = "center";
                td.style.color = "white";
                td.style.backgroundColor = "#e33c72";
                row.appendChild(td);

                td = document.createElement("td");
                td.style.backgroundColor = "#b1d7ae";

                input = document.createElement("input");
                input.type = "text";
                input.name = "song-" + code;
                input.size = 15;
                input.maxlength = 15;
                if (currSelections.hasOwnProperty(code)) {
                    input.value = currSelections[code];
                }
                input.addEventListener('change', function(evt) {
                    if (evt.target.name.indexOf("song-") == 0) {
                        var code = evt.target.name.substring(5);
                        currSelections[code] = evt.target.value;
                    }
                });
                td.appendChild(input);

                row.appendChild(td);
                if (pageLayout == 2) {
                    letter = nextLetter(letter);
                }
            }

            var para = document.createElement("p");
            para.appendChild(tbl);
            x.appendChild(para);
            if (pageLayout == 1) {
                letter = nextLetter(letter);
                letter = nextLetter(letter);
            } else if (pageLayout == 2) {
                letter = "A";
            }
        }

        document.getElementById("base_path").style.display = "block";
        document.getElementById("top_buttons").style.display = "block";
        document.getElementById("bottom_buttons").style.display = "block";
    }
    function populateDefaults() {
        var inputs = document.getElementsByTagName("input");
        for (var i = 0; i < inputs.length; i++) {
            if (inputs[i].name.indexOf("song-") == 0 && inputs[i].value.length == 0) {
                var code = inputs[i].name.substring(5);
                inputs[i].value = code + ".mp3";
                currSelections[code] = inputs[i].value;
            }
        }
    }
    function clearSongs() {
        var inputs = document.getElementsByTagName("input");
        for (var i = 0; i < inputs.length; i++) {
            if (inputs[i].name.indexOf("song-") == 0) {
                inputs[i].value = "";
            }
        }
        currSelections = {};
    }
    function fetchConfiguredSongs() {
        xhr.open("GET", "songlist.cgi");
        xhr.onreadystatechange=function() {
            if (xhr.readyState==4 && xhr.status>=200 && xhr.status<300) {
                var data=JSON.parse(xhr.responseText);
                currSelections = data;
                buildSongSheet();
            }
        }
        xhr.send();
    }
    window.onload=function(e) {
        document.getElementById("wallbox_type").value = currWallbox;
        document.getElementById("uri_base").value = currUriBase;
        fetchConfiguredSongs();
    };
</script>
</head>
<body>
    <div id="main">
    <font size="+2"><b>Wallbox Configuration</b></font><br/>
    <hr/>
    <form name="songform" action="songselect.cgi" method="post">
    <p>
        Wallbox Type:<br/>
        <select id="wallbox_type" name="wallbox" onchange="buildSongSheet()">
            <option value="UNKNOWN_WALLBOX" selected>Unknown</option>
            <option value="SEEBURG_3W1_100">Seeburg Wall-O-Matic 3W-1 100</option>
            <option value="SEEBURG_V3WA_200">Seeburg Wall-O-Matic V-3WA 200</option>
        </select>
    </p>
    <p>
        <div id="base_path">
        Base folder path:<br/>
        <input id="uri_base" type="text" name="uri-base" size="64" maxlength="255"/>
        </div>
    </p>
    <p>
        <div id="top_buttons">
        <button type="submit">Save Song Selections</button>
        <button type="button" onclick="populateDefaults()">Fill Empty Songs</button>
        <button type="button" onclick="clearSongs()">Clear All Selections</button>
        </div>
    </p>
    <div id="song_sheet"></div>
    <p>
        <div id="bottom_buttons">
        <button type="submit">Save Song Selections</button>
        <button type="button" onclick="populateDefaults()">Fill Empty Songs</button>
        <button type="button" onclick="clearSongs()">Clear All Selections</button>
        </div>
    </p>
    </form>
    </div>
</body>
</html>
