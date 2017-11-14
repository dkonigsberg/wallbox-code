<!doctype html>
<html>
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Wall-O-Matic</title>
<link rel="stylesheet" type="text/css" href="style.css"/>
</head>
<body>
<div id="main">
<font size="+2"><b>Wall-O-Matic Interface</b></font><br/>
<hr/>
<p>
    <div class="box-area" style="line-height: 28px; display:block">
        %WallboxType%<br/>
        %SonosZone%<br/>
    </div>
    <div class="box-area">
        Insert Coins<br/>
        <form method="post" action="/control/credit">
            <table class="coin-button-table"><tr>
            <td><button type="submit" class="coin-button" name="coin" value="5">Nickel</button></td>
            <td><button type="submit" class="coin-button" name="coin" value="10">Dime</button></td>
            <td><button type="submit" class="coin-button" name="coin" value="25">Quarter</button></td>
            </tr></table>
        </form>
    </div>
    <div class="box-area">
        <a href="/about">About</a>
        <a href="/wifi">Wi-Fi Setup</a>
        <a href="/sonos">Select Sonos Zone</a>
        <a href="/wallbox">Wallbox Configuration</a>
        <a href="/flash.html">Update Firmware</a>
    </div>
</p>
<hr/>
<center><img src="images/lprobe-logo.png"/><br/></center>
</div>
<center><font size="-1">
    <br/>
    Copyright &#169; 2017, Derek Konigsberg<br/>
    All rights reserved.
</font></center>
</body>
</html>
