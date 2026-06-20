# flipper-meshtastic
An app for the flipper zero that works with the rfm95w from Adafruit to create a functional node for meshtastic
<br>
<br>
# if you want to make this at home, please read the entire instructions below from start to finish before buying or doing anything so you know what you are doing.
<br>
# how to make it at home
<h3>first off, you need a flipper, a usb cable that can connect from your pc to your flipper, an sd card in your flipper, a soldering iron(highly recommended to use a wedge tip for soldering), soldering tin, one male to male jumper wire, 6 male to female jumper wires, 6 male pin headers, a paperclip or a proper 915mhz LoRa antenna that you know how to connect, and an rfm95w breakout board from https://www.adafruit.com/product/3072?srsltid=AfmBOoqABd6jpjQfJQPfLfAXuoDvxvfKwun1KpBl1ytZ675R-qUJ0RXz (this was previously on amazon, but now the only one on amazon is the 433mhz version. DO NOT USE THE 433MHZ VERSION AS IT WILL NOT WORK AND WILL PROBABLY CATCH ON FIRE!!!!!!! You may be able to search "adafruit 3072" on amazon to find it, but at the time of making this, it is not on amazon. do not buy the 3073 version as it is the 433mhz version).</h3>
<br>
<h3>then download the latest .fap file from [here](github.com/awesomefroggy29/flipper-meshtastic/releases)</h3>
<h3>now, plug in the usb cable to your pc and connect it to your flipper. go to https://lab.flipper.net/ and press connect. select your flipper zero in the popup.</h3>
<h3>now on the side bar, press files, then ext, then apps. now press the plus sign in the corner, and press create folder. name it "meshtastic", or whatever name you want. then go into the folder you created. now press the plus sign again and press upload file. now find where you downloaded "fliptastic.fap" and upload it.</h3>
<br>
<h1>the hard part</h1>
<h2>its soldering time.</h2>
<br>
<h3>im not going to tell you how to use a soldering iron, but if youve never used one before, please watch a video on how to solder small electronics. i am not responsible if you burn yourself while doing this.</h3>
<br>
<h3>for this, solder the short end of a pin header to each of the following labeled areas so the long end points down from the perspective of the top of the board: gnd, mosi, miso, cs, sck, and g0. for vin, solder one end of a jumper wire to the vin pin so that the other end of it is coming out of the top side of the board.</h3>
<h3>if you do not have a proper 915mhz lora antenna, you can use a paperclip. first, straighten the paperclip out as much as possible. then, cut from one end to as close as you can to 8.2cm in length. its better to have it over 8.2cm than under. we will use this piece as the antenna. if you want the antenna to go up parallel to the board, you can bend one end so that the bent part is around 3-4mm long.</h3>
<h3>now, solder that end(or one of the ends if you didnt want to bend it) to the antenna slot. the antenna slot is above the a at the end of LoRa on the bottom side of the front of the board.</h3>
<br>
<h3>after this, plug one jumper wire into each pin header that you soldered. you should figure out a way to wrap the wires around the whole module so that it can fit right on top of the flipper. if you dont want to do that, then you can just plug each wire to its corresponding pin on the flipper. the pin map is as follows: MOSI to pin 2/a7, MISO to pin 3/a6 (do not mix up MISO and MOSI!!!), CS to pin 4/a4, SCK to pin 5/b3, G0 to pin 6/b2, gnd to pin 8/gnd, and vin to pin 9/3v3. if you mix up any of these, it could break your flipper and rfm95w, so please double check!!</h3>
<br>

# this is how i wrapped my wires to fit:
<img width="384" height="512" alt="image" src="https://github.com/user-attachments/assets/d1429632-bd83-4453-a0ad-491a28bc4418" />
<img width="512" height="384" alt="image" src="https://github.com/user-attachments/assets/3e10238a-8f1d-4ad1-b2f0-fc81cba550e3" />
<br>
<h2>now if you did everything right, and each wire is plugged in(triple check that they are correct, you really do not want to break your flipper), then you can go into your apps on your flipper, scroll down to the meshtastic folder, and open fliptastic. if it says rfm95w not found, then you did something wrong. it could be that you didnt solder well, you didnt plug in the wires to the right places, you might have bought the 433mhz version of the radio module, the pins are loose, etc. check everything. if you see a menu with an inbox, then it worked. your flipper will automatically broadcast its name and id, and you can message people. if you go into settings, you get a variety of settings to change. if your area has a specific channel and frequency that many people use, you can set it there. you can also change your meshtastic name.</h2>

# hopefully this worked for you. if you like this project, consider starring it. thank you!


<h3>now for some information</h3>
<p>in the inbox when you send a message, it will start out with a "-" next to it. that means its waiting for acknowledgement. if it turns to a "=" then it was not acknowledged. if it turns to a "+", then it was. if you see a "<>" in someone's message, then it was probably an emoji or special character.</p>


