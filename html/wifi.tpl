<!doctype html>
<html>
<head>
<meta charset="utf-8"/>
<title>Wi-Fi Configuration</title>
<link rel="stylesheet" type="text/css" href="style.css"/>
<script type="text/javascript" src="js/140medley.min.js"></script>
<script type="text/javascript">

var xhr=j();
var currAp="%currSsid%";

function createInputForAp(ap) {
    if (ap.essid=="" && ap.rssi==0) return;
    var div=document.createElement("div");
    div.id="apdiv";
    var rssi=document.createElement("div");
    var rssiVal=-Math.floor(ap.rssi/51)*32;
    rssi.className="icon";
    rssi.style.backgroundPosition="0px "+rssiVal+"px";
    var encrypt=document.createElement("div");
    var encVal="-64"; //assume wpa/wpa2
    if (ap.enc=="0") encVal="0"; //open
    if (ap.enc=="1") encVal="-32"; //wep
    encrypt.className="icon";
    encrypt.style.backgroundPosition="-32px "+encVal+"px";
    var input=document.createElement("input");
    input.type="radio";
    input.name="essid";
    input.value=ap.essid;
    if (currAp==ap.essid) input.checked="1";
    input.id="opt-"+ap.essid;
    var label=document.createElement("label");
    label.htmlFor="opt-"+ap.essid;
    label.textContent=ap.essid;
    div.appendChild(input);
    div.appendChild(rssi);
    div.appendChild(encrypt);
    div.appendChild(label);
    return div;
}

function getSelectedEssid() {
    var e=document.forms.wifiform.elements;
    for (var i=0; i<e.length; i++) {
        if (e[i].type=="radio" && e[i].checked) return e[i].value;
    }
    return currAp;
}

function filterScanResults(apList) {
    var seen = {};
    return apList.sort(function(a, b) {
        return b.rssi - a.rssi;
    }).filter(function(item) {
        if (item.essid=="" && item.rssi==0) {
            return false;
        } else {
            return seen.hasOwnProperty(item.essid) ? false : (seen[item.essid] = true);
        }
    }).sort(function(a, b) {
        return a.essid.localeCompare(b.essid);
    });
}

function scanAPs() {
    xhr.open("GET", "wifiscan.cgi");
    xhr.onreadystatechange=function() {
        if (xhr.readyState==4 && xhr.status>=200 && xhr.status<300) {
            var data=JSON.parse(xhr.responseText);
            currAp=getSelectedEssid();
            if (data.result.inProgress=="0" && data.result.APs.length>1) {
                $("#aps").innerHTML="";
                var apList = filterScanResults(data.result.APs);
                for (var i=0; i<apList.length; i++) {
                    $("#aps").appendChild(createInputForAp(apList[i]));
                }
                window.setTimeout(scanAPs, 20000);
            } else {
                window.setTimeout(scanAPs, 1000);
            }
        }
    }
    xhr.send();
}

window.onload=function(e) {
    scanAPs();
};
</script>
</head>
<body>
<div id="main">
<font size="+2"><b>Wi-Fi Configuration</b></font><br/>
<hr/>
<p>
    Current WiFi network: %currSsid%
<p>
    Current WiFi mode: %WiFiMode%
</p>
<form name="wifiform" action="connect.cgi" method="post">
<p>
    To connect to a WiFi network, please select one of the detected networks...<br>
    <div id="aps">Scanning...</div>
    <br>
    WiFi password, if applicable: <br />
    <input type="password" name="passwd" value="%WiFiPasswd%"><br />
    <br />
    <input type="submit" name="connect" value="Connect!">
</p>
</div>
</body>
</html>
