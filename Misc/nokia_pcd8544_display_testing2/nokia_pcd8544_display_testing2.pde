/*
 * To use this sketch, connect the eight pins from your LCD like thus:
 *
 * Pin 1: Vcc -> +3.3V (rightmost, when facing the display head-on)
 * Pin 2: SCLK (clock) -> Arduino digital pin 3
 * Pin 3: SDIN (data-in) -> Arduino digital pin 4
 * Pin 4: D/C (data select) -> Arduino digital pin 5
 * Pin 5: SCE (enable) -> Arduino digital pin 7
 * Pin 6 -> Ground
 * Pin 7: Vout -> 10uF capacitor -> Ground
 * Pin 8: reset -> Arduino digital pin 6
 *
 * Since these LCDs are +3.3V devices, you have to add extra components to
 * connect it to the digital pins of the Arduino (not necessary if you are
 * using a 3.3V variant of the Arduino, such as Sparkfun's Arduino Pro).
 */


#include <PCD8544.h>


// A custom glyph (a smiley)...
static const byte glyph[] = { B00010000, B00110100, B00110000, B00110100, B00010000 };


static PCD8544 lcd;


void setup() {

    Serial.begin(57600);
    Serial.println("nokia_pcd8544_display_testing2");

    // PCD8544-compatible displays may have a different resolution...
    lcd.begin(84, 48);

    lcd.send( LOW, 0x21 );  // LCD Extended Commands.

    //doing nothing lcd.send( LOW, 0x81 );  // Set LCD Vop (Contrast). 
    //lcd.send( LOW, 0xB9 );  // Set LCD Vop (Contrast). 
    //lcd.send( LOW, 0xC2);   // default Vop (3.06 + 66 * 0.06 = 7V)
    //lcd.send( LOW, 0xE0);   // higher Vop for ST7576,  too faint at default

    //lcd.send( LOW, 0x04 );  // Set Temp coefficent. //0x04
    //lcd.send( LOW, 0x14 );  // LCD bias mode 1:48. //0x13
    //lcd.send( LOW, 0x0C );  // LCD in normal mode.

    lcd.send( LOW, 0x20 );  // LCD Extended Commands toggle

    // Add the smiley to position "0" of the ASCII table...
    lcd.createChar(0, glyph);
}


void loop() {
    // Just to show the program is alive...
    static int counter = 0;

    // Write a piece of text on the first line...
    lcd.setCursor(0, 0);
    lcd.print("Hello, World!");

    // Write the counter on the second line...
    lcd.setCursor(0, 1);
    lcd.print(counter, DEC);
    lcd.write(' ');
    lcd.write(0);  // write the smiley

    delay(500);  
    counter++;
}
