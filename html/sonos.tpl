<!doctype html>
<html>
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Sonos Configuration</title>
<link rel="stylesheet" type="text/css" href="style.css"/>
<script type="text/javascript" src="js/140medley.min.js"></script>
<script type="text/javascript">
    var xhr=j();
    var currUUID="%ZoneUUID%";

    function createInputForZone(zone) {
        var div=document.createElement("div");
        div.id="zdiv";
        var input=document.createElement("input");
        input.type="radio";
        input.name="uuid";
        input.value=zone.uuid;
        if (currUUID==zone.uuid) input.checked="1";
        input.id="opt-"+zone.uuid;
        var label=document.createElement("label");
        label.htmlFor="opt-"+zone.uuid;
        label.textContent=zone.zone_name;
        div.appendChild(input);
        div.appendChild(label);
        return div;
    }

    function showDiscoveredZones() {
        xhr.open("GET", "zonelist.cgi");
        xhr.onreadystatechange=function() {
            if (xhr.readyState==4 && xhr.status>=200 && xhr.status<300) {
                var data=JSON.parse(xhr.responseText);
                if (data.result.inProgress=="0" && data.result.zones.length>1) {
                    $("#zones").innerHTML="";
                    data.result.zones.sort((a, b) => a.zone_name.localeCompare(b.zone_name));
                    for (var i=0; i<data.result.zones.length; i++) {
                        $("#zones").appendChild(createInputForZone(data.result.zones[i]));
                    }
                    //window.setTimeout(showDiscoveredZones, 20000);
                } else {
                    //window.setTimeout(showDiscoveredZones, 1000);
                }
            }
        }
        xhr.send();
    }

    window.onload=function(e) {
        showDiscoveredZones();
    };
</script>
</head>
<body>
    <div id="main">
    <font size="+2"><b>Sonos Configuration</b></font><br/>
    <hr/>
    <p>
        Current Zone: %ZoneName%
    </p>
    <p>
        <form name="wifiform" action="zoneselect.cgi" method="post">
        Discovered Zones:<br/>
        <div id="zones">Discovering...</div><br/>
        <input type="submit" name="select" value="Select Zone"/>
        </form>
    </p>
    </div>
</body>
</html>
