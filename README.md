<img src="images/Storon%20Logo.png" alt="Storon Logo" width="1000">

Name credit to u/akir3y on reddit and chosen based on upvotes.

This project is a Voron printer stand that also stores filament. You have options for active dehumditifcation as well as the ability to feed your Voron (or any other printer really...) from within the stand using a [Filamentalist](https://github.com/Carrot-collective/ERCF_v2/tree/master/Recommended_Options/Filamentalist_Rewinder/Filamentalist_FV3_Rewinder) passive rewinder and an [lll-plus](https://mellow.klipper.cn/en/docs/category/fly-lll-plus-%E7%BC%93%E5%86%B2%E5%99%A8) buffer.

** DISCLAIMER - This is a work in progress. I am trying to add and make things better once project at a time. Some of what i used was because i already owned it. I have 24V to the buffer, 12V to the lights im using, 3V to the micro-dehumidifier, 5v to the ESP32, etc. Things are all over the place because its what i had in my hand when iw as testing and haven't replaced it. I am hoping i can make a video about this whole assembly next year and we can build one out from scratch. When i do that i'd like to have some more uniformity in power supplies and maybe even do a custom little PCB labelled properly for the functions just to make it easier to build out. 

In the works and being tested is RFID reading of spools using an ESP32 inside the cabinet. Each lane (the spool feeding a toolhead) gets its own RFID reader, so the stand knows which spool is on which tool and keeps [Spoolman](https://github.com/Donkie/Spoolman) up to date on its own. Full details are in the [ESP32 Inside Cabinet README](ESP32%20Inside%20Cabinet/README.md). The basics are below.

**RFID hardware**
- ESP32-S3 DevKitC-1 (N16R8) on a 44-pin screw-terminal breakout.
- One RC522 RFID reader per lane (up to 6 electrically, space to mount 6 with current dimensions would be tough), mounted beside the spool facing its side. A ferrite sheet behind each reader keeps it from reading the neighboring spool.
- Readers are wired with two Cat5 cables, each daisy-chained through up to 3 readers. All readers share the SPI bus, and each lane has its own select (SS) pin. Each cable's clock line gets a 39 Ω resistor at the ESP32 end.
- Bambu Lab spools are read straight from their factory tags (read-only). Any other spool gets a 25 mm NTAG215 sticker, written in the open [OpenTag3D](https://opentag3d.info) format from the web UI.
- The same ESP32 also runs the two DHT22 humidity/temperature sensors and the circulation fan.

**Setup basics**
1. Flash the prebuilt firmware from [Releases](https://github.com/astechevolves/storon/releases). No code edits needed.
2. Join the `StorOn-Setup-XXXX` WiFi hotspot (password `storonsetup`) and the settings page opens.
3. Enter your WiFi, MQTT broker, Spoolman URL and (optionally) Moonraker URL.
4. Set up the lanes (see below), then **Save and reboot**. The web UI is then at http://filament-monitor.local/.

**Setting lanes**
- There are up to six lane slots. For each one, set whether a reader is fitted, its SS pin, and which tool (`T0`, `T1`, ...) it feeds.
- Lane numbers stay fixed even when other lanes are turned off, so a lane always means the same physical spot in the cabinet.
- The **Readers** page tests one reader at a time and tells you whether a problem is the wiring or the reader itself. Use it after wiring each lane.

**Spoolman integration**
- In Spoolman, first create a **Text** extra field on spools with the key `tag`. This is the same field OpenSpoolMan uses, so spools already set up there will match.
- When a spool is placed in a lane, its tag is matched to a Spoolman spool and that spool's location is set to `StorOn Lane N`. When it's removed, the location is cleared (or set to a location you choose). This load/unload are in the Klipper Macros to set and unset the lane assignemnts. 
- An unknown tag can be linked to an existing spool, or a new spool can be created from the tag data, from the lane's **Spoolman** button on the status page.
- Remaining weight comes from Spoolman. Moonraker deducts filament as you print, and the ESP32 re-reads each lane's spool so the weight shown stays current.
- With Moonraker set, each tool's spool is also shared with OrcaSlicer and HelixScreen, and the web UI gets Load/Unload buttons per lane that can be set to custom macros in settings.

**Electronics and software**
- [ESP32 Inside Cabinet](ESP32%20Inside%20Cabinet/README.md): humidity/temperature monitoring, fan, per-lane RFID spool tags (Bambu and OpenTag3D), Spoolman sync, and a web UI.
- [Klipper Macros](Klipper%20Macros/README.md): tracks each tool's loaded/unloaded state for the ESP32 and blocks prints on unloaded tools.
- OrcaSlicer: the [Orca Spoolman Presets](https://github.com/DIEHARDave/orca_spoolman_presets) plugin (by DIEHARDave) fills the filament slots from the spool loaded on each tool, using the `lane_data` the ESP32 writes.


<img src="images/With%20Filament%20Out.jpg" alt="With Filament Out" width="500"> <img src="images/Side%20View.jpg" alt="Side View" width="500">

<img src="images/Rack%20Drawer%20Slides.jpg" alt="Rack Drawer Slides" width="500"> <img src="images/Back%20Panel.jpg" alt="Back Panel" width="500">


At the dimensions used for my build there can be up to 14-16 spools on each drawer (depending on spool width) and i have 2 drawers installed for a total of 28-32 spools of storage. I currently have 5 spools across the top for the toolheads and the buffers are mounted below them. Using [custom lll-plus firmware](https://github.com/astechevolves/lll-buffed) with I2C controls i am connecting a single USB cable to the RasPi running klipper and controlling/monitoring the buffers. 

Some used parts:\
Rack Slides: I did not purchase any as i had some on hand. If i build another one ill need to buy some to test fits. \
2020 Aluminum Extrusion: https://a.co/d/0fAiQeXf \
2040 Aluminum Extrusion: https://a.co/d/03j1iFax \
Corner Brackets: https://a.co/d/0c3Umoue \
3 Side Corners: https://a.co/d/0ic4IAEs \
Profile L Brackets: https://a.co/d/0b12QCzn \
M5 Nuts and Screws: https://a.co/d/0fcr5ywm \
Sealing Foam: https://a.co/d/0fpPBf9j

Hardware for [Clicky Clack Door](https://github.com/tanaes/whopping_Voron_mods/blob/main/clickyclacky_door%2FREADME.md). Used 3 hinges instead of two and the door panel and sealing was longer for the larger door.  

Pin Bushings for Clicky Clack: https://a.co/d/01oHRWRj

If you want monitoring some ESP32 hardware will be needed too. How you monitor that data may change. I am publishing MQTT updates to Home Assistant. 

Power supply for dehumidifier, 3VDC required: https://a.co/d/0h5Y2pVP \
MDL-5 Dehumdifier: https://micro-dehumidifier.com/products-rosahl/membranes/ \
Wiring Break out for I2C Buss on Buffers: https://a.co/d/03XT3YTJ

PTFE Connectors: https://a.co/d/0i8aAcsk

[LLL-Plus Buffer](https://3dmellow.com/products/mellow-lll-plus-filament-buffer-for-diy-3d-printers-klipper-rrf-marlin-material-break-detection-automatic-filament-feeding?VariantsId=10247), either whole and assembled or a kit and print your own parts. If you want to self assembly a good guide was [written here](https://www.teamfdm.com/forums/topic/7953-mellow-fly-lll-pro-filament-buffer/#comment-33137).

Physical modifications have been made as outlined below and new firmware for I2C control via a single USB connection. 
New Firmware can [be found here](https://github.com/astechevolves/lll-buffed). 

The first mod was making the buttons taller so you can find them blind by reaching under the buffer which is mounted facing down. 

<img src="images/lll-plus/Buttons.jpg" alt="Buttons" width="400">

The second mod was to change the buffer to allow an ECAS connector. The PTFE tube port was very weak and if the buffer pushed ont he filament at all it would push the tubing out of the connector all together. This was annoying because then the buffer would drop and the filament may feed into the back of the box until the timeout was reached. Not ideal. 

<img src="images/lll-plus/Old%20PTFE%20Fitting.jpg" alt="Crappy Collar" width="400">

The third mod was to install a tougher spring ont he buffer tube. The weak spring meant any resistance ont he filament up to the print could cause sufficient back tension and that would mean the filament stops feeding. A strong spring means you are more likely to make it all the way to the head because it takes more back tension to fully extend the buffer.

<img src="images/lll-plus/Spring%20Comparison.jpg" alt="Springs" width="400">

<img src="images/lll-plus/Final%20Buffer.jpg" alt="With new parts installed" width="600">

Hardware for [Filmentalist Rollers](https://github.com/Carrot-collective/ERCF_v2/tree/master/Recommended_Options/Filamentalist_Rewinder/Filamentalist_FV3_Rewinder). \
Filamentalist one way bearings: https://a.co/d/0h9YtOMt \
Filamentalist Springs: https://a.co/d/0beVFlb8 \
608 Bearings: https://a.co/d/0bcoLnwL \
688 Bearings: https://a.co/d/02Pbp8nU


