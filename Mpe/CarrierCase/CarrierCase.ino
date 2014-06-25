/*
CarrierCase

- Generally based on roomNode, probably can be made compatible with other environmental sensor nodes.
- Two moisture probes.
- Later to have shift register and analog switchting for mre inputs, perhaps.
  Make SDA/SCL free for I2C or other bus?

Current pin assignments:

Ard Atm Jee  Sign Mapping
=== === ==== ==== ===========
D0  PD0      TXD  SER/DEBUG  
D1  PD1      RXD  SER/DEBUG  
D2  PD2      INT0 JeePort IRQ
D3  PD3      INT1 RFM12B IRQ 
D4  PD4 DIO1      PIR        
D5  PD5 DIO2                 
D6  PD6 DIO3                 
D7  PD7 DIO4      DHT11      
D8  PB0                      
D9  PB1                      
D10 PB2      SS   RFM12B SEL 
D11 PB3      MOSI RFM12B SDI 
D12 PB4      MISO RFM12B DSO 
D13 PB5      SCK  RFM12B SCK 
A0  PC1 AIO1                 
A1  PC1 AIO2                 
A2  PC2 AIO3                 
A1  PC3 AIO4      LDR        
A4  PC4      SDA             
A5  PC5      SCL             
    PC6      RST  Reset      
=== === ==== ==== ===========


Todo:

Ard Atm Jee  Sign Mapping
=== === ==== ==== ===========
D4  PD4 DIO1      MP-1 RIGHT 
D5  PD5 DIO2      MP-1 LEFT  
D6  PD6 DIO3      MP-2 RIGHT 
D7  PD7 DIO4      MP-2 LEFT  
D8  PB0           MP-1 LED   
D9  PB1           MP-2 LED   
A0  PC1 AIO1      MP1 measure
A1  PC1 AIO2      MP2 measure
A2  PC2 AIO3                 
A1  PC3 AIO4      LDR        
A4  PC4      SDA  Dallas bus 
A5  PC5      SCL  DHT out    
=== === ==== ==== ===========

ToDo
  - Announce payload config

  	spec:
		detect:
			PIR
				motion
			LDR
				
  - 
*/

//#ifdef MYCMDLINE

#define DEBUG_DHT 1
#define _EEPROMEX_DEBUG 1  // Enables logging of maximum of writes and out-of-memory

#include <stdlib.h>
#include <avr/eeprom.h>
#include <avr/pgmspace.h>
#include <avr/sleep.h>
#include <util/atomic.h>

#include <DHT.h> // Adafruit DHT
#include <JeeLib.h>
#include <OneWire.h>
//#include <EEPROM.h>
// include EEPROMEx.h

#include "EmBencode.h"

/** Globals and sketch configuration  */
#define DEBUG           1 /* Enable trace statements */
#define SERIAL          1 /* Enable serial */
							
#define _MEM            1   // Report free memory 
#define _RFM12LOBAT      1   // Use JeeNode lowbat measurement
#define _DHT            1
#define DHT_PIN         7   // DIO for DHTxx
#define _DS             1
#define DS_PIN          A5
#define DEBUG_DS        1
//#define FIND_DS    1
#define LDR_PORT        4   // defined if LDR is connected to a port's AIO pin
#define PIR_PORT        1
							
#define MEASURE_PERIOD  50  // how often to measure, in tenths of seconds
#define REPORT_EVERY    5   // report every N measurement cycles
#define SMOOTH          5   // smoothing factor used for running averages
#define RETRY_PERIOD    10  // how soon to retry if ACK didn't come in
#define RETRY_LIMIT     2   // maximum number of times to retry
#define ACK_TIME        10  // number of milliseconds to wait for an ack
#define MAXLENLINE      79
#define SRAM_SIZE       0x800 // atmega328, for debugging
#define RADIO_SYNC_MODE 2
							

String sketch = "CarrierCase";
String version = "0";

String node_id = "cc-1";

int pos = 0;

MpeSerial mpeser (57600);

/* Scheduled tasks */
enum { ANNOUNCE, MEASURE, REPORT };

static word schedbuf[REPORT];
Scheduler scheduler (schedbuf, REPORT);

// has to be defined because we're using the watchdog for low-power waiting
ISR(WDT_vect) { Sleepy::watchdogEvent(); }

/* RF12 message types */
enum { ANNOUNCE_MSG, REPORT_MSG };


/* Atmega EEPROM stuff */
const long maxAllowedWrites = 100000; /* if evenly distributed, the ATmega328 EEPROM 
should have at least 100,000 writes */
const int memBase          = 0;
//const int memCeiling       = EEPROMSizeATmega328;


#if _DS
/* Dallas OneWire bus with registration for DS18B20 temperature sensors */
OneWire ds(DS_PIN);

uint8_t ds_count = 0;
uint8_t ds_search = 0;
volatile int ds_value[ds_count];
volatile int ds_value[8]; // take on 8 DS sensors in report

enum { DS_OK, DS_ERR_CRC };
#endif

#if LDR_PORT
Port ldr (LDR_PORT);
#endif

#if _DHT
//DHTxx dht (DHT_PIN); // JeeLib DHT

#define DHTTYPE DHT11   // DHT 11 
//#define DHTTYPE DHT22   // DHT 22  (AM2302)

DHT dht (DHT_PIN, DHTTYPE); // DHT lib
#endif

/* Report variables */

static byte reportCount;    // count up until next report, i.e. packet send
static byte rf12_id;       // node ID used for this unit

// This defines the structure of the packets which get sent out by wireless:

struct {
	int msgtype     :8; // XXX: incorporated, but not the right place..
#if LDR_PORT
	byte light      :8;     // light sensor: 0..255
#endif
#if PIR_PORT
	byte moved      :1;  // motion detector: 0..1
#endif
#if _DHT
	int rhum        :7;  // rhumdity: 0..100 (4 or 5% resolution?)
	int temp        :10; // temperature: -500..+500 (tenths, .5 resolution)
#endif
	int ctemp       :10; // atmega temperature: -500..+500 (tenths)
#if _MEM
	int memfree     :16;
#endif
#if _RFM12LOBAT
	byte lobat      :1;  // supply voltage dropped under 3.1V: 0..1
#endif
} payload;


/** AVR routines */

int freeRam () {
	extern int __heap_start, *__brkval; 
	int v;
	return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}

int usedRam () {
	return SRAM_SIZE - freeRam();
}


/** ATmega routines */

double internalTemp(void)
{
	unsigned int wADC;
	double t;

	// The internal temperature has to be used
	// with the internal reference of 1.1V.
	// Channel 8 can not be selected with
	// the analogRead function yet.

	// Set the internal reference and mux.
	ADMUX = (_BV(REFS1) | _BV(REFS0) | _BV(MUX3));
	ADCSRA |= _BV(ADEN);  // enable the ADC

	delay(20);            // wait for voltages to become stable.

	ADCSRA |= _BV(ADSC);  // Start the ADC

	// Detect end-of-conversion
	while (bit_is_set(ADCSRA,ADSC));

	// Reading register "ADCW" takes care of how to read ADCL and ADCH.
	wADC = ADCW;

	// The offset of 324.31 could be wrong. It is just an indication.
	t = (wADC - 311 ) / 1.22;

	// The returned temperature is in degrees Celcius.
	return (t);
}


/** Generic routines */

static void serialFlush () {
#if SERIAL
#if ARDUINO >= 100
	Serial.flush();
#endif
	delay(2); // make sure tx buf is empty before going back to sleep
#endif
}

void blink(int led, int count, int length) {
	for (int i=0;i<count;i++) {
		digitalWrite (led, HIGH);
		delay(length);
		digitalWrite (led, LOW);
		delay(length);
	}
}

static void showNibble (byte nibble) {
	char c = '0' + (nibble & 0x0F);
	if (c > '9')
		c += 7;
	Serial.print(c);
}

bool useHex = 0;

static void showByte (byte value) {
	if (useHex) {
		showNibble(value >> 4);
		showNibble(value);
	} else {
		Serial.print((int) value);
	}
}

void debug_ticks(void)
{
#if SERIAL && DEBUG
	tick++;
	if ((tick % 20) == 0) {
		Serial.print('.');
		pos++;
	}
	if (pos > MAXLENLINE) {
		pos = 0;
		Serial.println();
	}
	serialFlush();
#endif
}

// utility code to perform simple smoothing as a running average
static int smoothedAverage(int prev, int next, byte firstTime =0) {
	if (firstTime)
		return next;
	return ((SMOOTH - 1) * prev + next + SMOOTH / 2) / SMOOTH;
}

// spend a little time in power down mode while the SHT11 does a measurement
static void lpDelay () {
	Sleepy::loseSomeTime(32); // must wait at least 20 ms
}


String serialBuffer = "";         // a string to hold incoming data

void serialEvent() {
	while (Serial.available()) {
		// get the new byte:
		char inchar = (char)Serial.read(); 
		// add it to the serialBuffer:
		if (inchar == '\n') {
			serialBuffer = "";
		} 
		else if (serialBuffer == "new ") {
			node_id += inchar;
		} 
		else {
			serialBuffer += inchar;
		}
	}
}


/* embenc for storage and xfer proto */

// unused?
uint8_t* embencBuff;
int embencBuffLen = 0;

// dynamic allocation for embencBuff
void embenc_push(char ch) {
	embencBuffLen += 1;
	uint8_t* np = (uint8_t *) realloc( embencBuff, sizeof(uint8_t) * embencBuffLen);
	if( np != NULL ) { 
		embencBuff = np; 
		embencBuff[embencBuffLen] = ch;
	} else {
		Serial.println(F("Out of Memory"));
	}
}

//void EmBencode::PushChar(char ch) {
//	Serial.write(ch);
//	embenc_push(ch);
//}

void freeEmbencBuff() {
	embencBuff = (uint8_t *) realloc(embencBuff, 0); // free??
	embencBuffLen = 0;
}

/** RF12 routines */
static bool waitForAck() {
    MilliTimer ackTimer;
    while (!ackTimer.poll(ACK_TIME)) {
        if (rf12_recvDone() && rf12_crc == 0 &&
                // see http://talk.jeelabs.net/topic/811#post-4712
                rf12_hdr == (RF12_HDR_DST | RF12_HDR_CTL | rf12_id))
            return 1;
        set_sleep_mode(SLEEP_MODE_IDLE);
        sleep_mode();
    }
    return 0;
}

#if PIR_PORT

#define PIR_INVERTED    0   // 0 or 1, to match PIR reporting high or low
#define PIR_HOLD_TIME   30  // hold PIR value this many seconds after change
/// Interface to a Passive Infrared motion sensor.
    class PIR : public Port {
        volatile byte value, changed;
    public:
        volatile uint32_t lastOn;
        PIR (byte portnum)
            : Port (portnum), value (0), changed (0), lastOn (0) {}

        // this code is called from the pin-change interrupt handler
        void poll() {
            // see http://talk.jeelabs.net/topic/811#post-4734 for PIR_INVERTED
            byte pin = digiRead() ^ PIR_INVERTED;
            // if the pin just went on, then set the changed flag to report it
            if (pin) {
                if (!state())
                    changed = 1;
                lastOn = millis();
            }
            value = pin;
        }

        // state is true if curr value is still on or if it was on recently
        byte state() const {
            byte f = value;
            if (lastOn > 0)
                ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
                    if (millis() - lastOn < 1000 * PIR_HOLD_TIME)
                        f = 1;
                }
            return f;
        }

        // return true if there is new motion to report
        byte triggered() {
            byte f = changed;
            changed = 0;
            return f;
        }
    };

PIR pir (PIR_PORT);

// the PIR signal comes in via a pin-change interrupt
ISR(PCINT2_vect) { pir.poll(); }

// send packet and wait for ack when there is a motion trigger
static void doTrigger() {
    #if DEBUG
        Serial.print("PIR ");
        Serial.print((int) payload.moved);
		Serial.print(' ');
        Serial.print((int) pir.lastOn);
		Serial.println(' ');
        serialFlush();
    #endif

    for (byte i = 0; i < RETRY_LIMIT; ++i) {
        rf12_sleep(RF12_WAKEUP);
        rf12_sendNow(RF12_HDR_ACK, &payload, sizeof payload);
        rf12_sendWait(RADIO_SYNC_MODE);
        byte acked = waitForAck();
        rf12_sleep(RF12_SLEEP);

        if (acked) {
            #if DEBUG
                Serial.print(" ack ");
                Serial.println((int) i);
                serialFlush();
            #endif
            // doReset scheduling to start a fresh measurement cycle
            scheduler.timer(MEASURE, MEASURE_PERIOD);
            return;
        }
        
        delay(RETRY_PERIOD * 100);
    }
    scheduler.timer(MEASURE, MEASURE_PERIOD);
    #if DEBUG
        Serial.println(" no ack!");
        serialFlush();
    #endif
}
#endif


// XXX: just an example, not actually used 
static void sendSomeData () {
  EmBencode encoder;
  // send a simple string
  encoder.push("abcde");
  // send a number of bytes, could be binary
  encoder.push("123", 3);
  // send an integer
  encoder.push(12345);
  // send a list with an int, a nested list, and an int
  encoder.startList();
    encoder.push(987);
    encoder.startList();
      encoder.push(654);
    encoder.endList();
    encoder.push(321);
  encoder.endList();
  // send a large integer
  encoder.push(999999999);
  // send a dictionary with two entries
  encoder.startDict();
    encoder.push("one");
    encoder.push(11);
    encoder.push("two");
    encoder.push(22);
  encoder.endDict();
  // send one last string
  encoder.push("bye!");
}

static void writeConfig() {
	eeprom_write_byte(RF12_EEPROM_ADDR, 4);
	eeprom_write_byte(RF12_EEPROM_ADDR+1, 5);
	//	eeprom_write_byte(RF12_EEPROM_ADDR+2, );
	// JeeLib RF12_EEPROM_ADDR is at 20
	int nodeId = eeprom_read_byte(RF12_EEPROM_ADDR);
	int group = eeprom_read_byte(RF12_EEPROM_ADDR + 1);

	Serial.print("nodeId: ");
	Serial.println(nodeId);
	Serial.print("group: ");
	Serial.println(group);
	serialFlush();
}

#if _DS
/* Dallas DS18B20 thermometer */

static int ds_readdata(uint8_t addr[8], uint8_t data[12]) {
	byte i;
	byte present = 0;

	ds.doReset();
	ds.select(addr);
	ds.write(0x44,1);         // start conversion, with parasite power on at the end

	serialFlush();
	Sleepy::loseSomeTime(800); 
	//delay(1000);     // maybe 750ms is enough, maybe not
	// we might do a ds.depower() here, but the doReset will take care of it.

	present = ds.doReset();
	ds.select(addr);
	ds.write(0xBE);         // Read Scratchpad

#if SERIAL && DEBUG_DS
	Serial.print(F("Data="));
	Serial.print(present,HEX);
	Serial.print(" ");
#endif
	for ( i = 0; i < 9; i++) {           // we need 9 bytes
		data[i] = ds.read();
#if SERIAL && DEBUG_DS
		Serial.print(i);
		Serial.print(':');
		Serial.print(data[i], HEX);
		Serial.print(" ");
#endif
	}

	uint8_t crc8 = OneWire::crc8( data, 8);

#if SERIAL && DEBUG_DS
	Serial.print(F(" CRC="));
	Serial.print( crc8, HEX);
	Serial.println();
	serialFlush();
#endif

	if (crc8 != data[8]) {
		return DS_ERR_CRC; 
	} else { 
		return DS_OK; 
	}
}

static int ds_conv_temp_c(uint8_t data[8], int SignBit) {
	int HighByte, LowByte, TReading, Tc_100;
	LowByte = data[0];
	HighByte = data[1];
	TReading = (HighByte << 8) + LowByte;
	SignBit = TReading & 0x8000;  // test most sig bit
	if (SignBit) // negative
	{
		TReading = (TReading ^ 0xffff) + 1; // 2's comp
	}
	Tc_100 = (6 * TReading) + TReading / 4;    // multiply by (100 * 0.0625) or 6.25
	return Tc_100;
}

// FIXME: returns 8500 value at times, drained parasitic power?
static int readDS18B20(uint8_t addr[8]) {
	byte data[12];
	int SignBit;

	int result = ds_readdata(addr, data);	
	
	if (result != DS_OK) {
#if SERIAL
		Serial.println(F("CRC error in ds_readdata"));
		serialFlush();
#endif
		return 0;
	}

	int Tc_100 = ds_conv_temp_c(data, SignBit);

	if (SignBit) {
		return 0 - Tc_100;
	} else {
		return Tc_100;
	}
}

static uint8_t readDSCount() {
	uint8_t ds_count = EEPROM.read(3);
	if (ds_count == 0xFF) return 0;
	return ds_count;
}

static void updateDSCount(uint8_t new_count) {
	if (new_count != ds_count) {
		EEPROM.write(3, new_count);
		ds_count = new_count;
	}
}

static void writeDSAddr(uint8_t addr[8]) {
	int l = 4 + ( ( ds_search-1 ) * 8 );
	for (int i=0;i<8;i++) {
		EEPROM.write(l+i, addr[i]);
	}
}

static void readDSAddr(int a, uint8_t addr[8]) {
	int l = 4 + ( a * 8 );
	for (int i=0;i<8;i++) {
		addr[i] = EEPROM.read(l+i);
	}
}

static void printDSAddrs() {
	for (int q=0;q<ds_count;q++) {
		Serial.print("Mem Address=");
		int l = 4 + ( q * 8 );
		int r[8];
		for (int i=0;i<8;i++) {
			r[i] = EEPROM.read(l+i);
			Serial.print(i);
			Serial.print(':');
			Serial.print(r[i], HEX);
			Serial.print(' ');
		}
		Serial.println();
	}
}

static void findDS18B20s(void) {
	byte i;
	byte addr[8];

	if (!ds.search(addr)) {
#if SERIAL && DEBUG_DS
		Serial.println("No more addresses.");
#endif
		ds.reset_search();
		if (ds_search != ds_count) {
#if DEBUG || DEBUG_DS
			Serial.print("Found ");
			Serial.print(ds_search );
			Serial.println(" addresses.");
			Serial.print("Previous search found ");
			Serial.print(ds_count);
			Serial.println(" addresses.");
#endif
		}
		updateDSCount(ds_search);
		ds_search = 0;
		return;
	}

	if ( OneWire::crc8( addr, 7) != addr[7]) {
#if DEBUG || DEBUG_DS
		Serial.println("CRC is not valid!");
#endif
		return;
	}

	ds_search++;

#if DEBUG || DEBUG_DS
	Serial.print("New Address=");
	for( i = 0; i < 8; i++) {
		Serial.print(i);
		Serial.print(':');
		Serial.print(addr[i], HEX);
		Serial.print(" ");
	}
	Serial.println();
#endif

	writeDSAddr(addr);
}



#endif // _DS

/* Initialization routines */

void doConfig(void)
{
	// JeeLib RF12_EEPROM_ADDR is at 20
	int nodeId = eeprom_read_byte(RF12_EEPROM_ADDR);
	int group = eeprom_read_byte(RF12_EEPROM_ADDR + 1);

	// Read embencoded from eeprom
	int specByteCnt= eeprom_read_byte(RF12_EEPROM_ADDR + 2);
	uint8_t specRaw [specByteCnt]; 
	eeprom_read_block( 
			(void*)specRaw, 
			(const void*)RF12_EEPROM_ADDR + 3,
			specByteCnt 
		);

	char embuf [200];
	EmBdecode decoder (embuf, sizeof embuf);
	uint8_t bytes;
	int i = 0;
	// XXX: fix this, catch longer than scenario, check for remaining raw spec
	while (!bytes) {
		bytes = decoder.process(specRaw[i]);
		i += 1;
	}

	if (!bytes) {
		Serial.println("Unable to decode");
	} else {
		Serial.print(" => ");
		Serial.print((int) bytes);
		Serial.println(" bytes");
		for (;;) {
			uint8_t token = decoder.nextToken();
			if (token == EmBdecode::T_END)
				break;
			switch (token) {
				case EmBdecode::T_STRING:
					Serial.print(" string: ");
					Serial.println(decoder.asString());
					break;
				case EmBdecode::T_NUMBER:
					Serial.print(" number: ");
					Serial.println(decoder.asNumber());
					break;
				case EmBdecode::T_DICT:
					Serial.println(" > dict");
					break;
				case EmBdecode::T_LIST:
					Serial.println(" > list");
					break;
				case EmBdecode::T_POP:
					Serial.println(" < pop");
					break;
			}
		}
		decoder.doReset();
	}
}

void initLibs()
{
}

void doReset(void)
{
	pinMode(ledPin, OUTPUT);
	pinMode(backlightPin, OUTPUT);
	digitalWrite(backlightPin, LOW);
	tick = 0;
	reportCount = REPORT_EVERY;     // report right away for easy debugging
	scheduler.timer(ANNOUNCE, 0);
}

/* Run-time handlers */

bool doAnnounce()
{
#if SERIAL

	//mpeser.startAnnounce(sketch, version);

#if LDR_PORT
	Serial.print(" ldr");
#endif
#if _DHT
	dht.begin();
	Serial.print(" dht11-rhum dht11-temp");
#endif
	Serial.print(" attemp");

#if _DS 
	ds_count = readDSCount();
	for ( int i = 0; i < ds_count; i++) {
		Serial.print(" ds-");
		Serial.print(i+1);
	}
#endif

#endif //SERIAL


	embenc_push(ANNOUNCE_MSG);

	EmBencode payload_spec;

	payload_spec.startList();
#if LDR_PORT
	payload_spec.startList();
	// name, may be unique but can be part number and as specific or generic as possible
	payload_spec.push("ldr2250"); 
	// bits in payload
	payload_spec.push(8);
	// XXX decoder params, optional?
	payload_spec.push("int(0-255)");
	payload_spec.endList();
#if SERIAL
	Serial.print(F(" ldr"));
#endif
#endif
#if PIR_PORT
	payload_spec.startList();
	payload_spec.push("moved");
	payload_spec.push(1);
	payload_spec.endList();
#if SERIAL
	Serial.print("moved ");
#endif
#endif
#if _DHT
	payload_spec.startList();
	payload_spec.push("dht11-rhum");
	payload_spec.push(7);
	payload_spec.endList();
#if SERIAL
	Serial.print(F(" dht11-rhum dht11-temp"));
#endif
#endif // _DHT
	payload_spec.startList();
	payload_spec.push("ctemp");
	payload_spec.push(10);
	payload_spec.endList();
#if SERIAL
	Serial.print(F(" ctemp"));
#endif
#if _MEM
	payload_spec.startList();
	payload_spec.push("memfree");
	payload_spec.push(16);
	payload_spec.endList();
#if SERIAL
	Serial.print(F(" memfree"));
#endif
#endif
	payload_spec.startList();
	payload_spec.push("lobat");
	payload_spec.endList();
#if SERIAL
	Serial.println(F(" lobat"));
	serialFlush();
#endif
	payload_spec.endList();

	Serial.println();
	for (int i=0; i<embencBuffLen; i++) {
		//showByte(embencBuff[i]);
		if (i > 0) Serial.print(' ');
		Serial.print((int)embencBuff[i]);
	}
	Serial.println();
	Serial.print(F("Sending"));
	Serial.print((int)embencBuffLen);
	Serial.println(F("bytes"));
	serialFlush();

	if (embencBuffLen > 66) {
	// FIXME: need to send in chunks
	}

	rf12_sleep(RF12_WAKEUP);
	rf12_sendNow(
		(rf12_id & RF12_HDR_MASK) | RF12_HDR_ACK,
		&embencBuff, 
		sizeof embencBuff);
	rf12_sendWait(RADIO_SYNC_MODE);
	bool acked = waitForAck();
	
	acked = 1;

	freeEmbencBuff();

	if (acked) {
//		Serial.println("ACK");
//	} else {
//		Serial.println("NACK");
	}
	Serial.print(F("Free RAM: "));
	Serial.println(freeRam());
	serialFlush();

	rf12_sleep(RF12_SLEEP);
	return acked;

#if _DS
			// report a new node or reinitialize node with central link node
//			for ( int x=0; x<ds_count; x++) {
//				Serial.print("SEN ");
//				Serial.print(node_id);
//				Serial.print(" ds-");
//				Serial.print(x);
//				for ( int y= 0; y<9; y++) {           // we need 9 bytes
//					Serial.print(" ");
//					Serial.print(ds_addr[x][y], HEX);
//				}
//				Serial.println("");
//			}
#endif
}

// readout all the sensors and other values
void doMeasure()
{
	byte firstTime = payload.ctemp == 0; // special case to init running avg

	int ctemp = internalTemp();
	payload.ctemp = smoothedAverage(payload.ctemp, ctemp, firstTime);
#if SERIAL && DEBUG_MEASURE
	Serial.println();
	Serial.print("AVR T new/avg ");
	Serial.print(ctemp);
	Serial.print(' ');
	Serial.println(payload.ctemp);
#endif

#if _DS
	uint8_t addr[8];
	for ( int i = 0; i < ds_count; i++) {
		readDSAddr(i, addr);
		ds_value[i] = readDS18B20(addr);
	}
#endif

#if _RFM12LOBAT
	payload.lobat = rf12_lowbat();
#if SERIAL && DEBUG_MEASURE
	if (payload.lobat) {
		Serial.println("Low battery");
	}
#endif
#endif

#if PIR_PORT
	payload.moved = pir.state();
#endif

#if LDR_PORT
	//ldr.digiWrite2(1);  // enable AIO pull-up
	byte light = ~ ldr.anaRead() >> 2;
	ldr.digiWrite2(0);  // disable pull-up to reduce current draw
	payload.light = smoothedAverage(payload.light, light, firstTime);
#endif //LDR

#if _DHT
	float h = dht.readHumidity();
	float t = dht.readTemperature();
	if (isnan(h)) {
#if SERIAL | DEBUG
		Serial.println(F("Failed to read DHT11 humidity"));
#endif
	} else {
		payload.rhum = smoothedAverage(payload.rhum, round(h), firstTime);
#if SERIAL && DEBUG_MEASURE
		Serial.print(F("DHT RH new/avg "));
		Serial.print(h);
		Serial.print(' ');
		Serial.println(payload.rhum);
#endif
	}
	if (isnan(t)) {
#if SERIAL | DEBUG
		Serial.println(F("Failed to read DHT11 temperature"));
#endif
	} else {
		payload.temp = smoothedAverage(payload.temp, (int)(t * 10), firstTime);
#if SERIAL && DEBUG_MEASURE
		Serial.print(F("DHT T new/avg "));
		Serial.print(t);
		Serial.print(' ');
		Serial.println(payload.temp);
#endif
	}
#endif // _DHT

#if _MEM
	payload.memfree = freeRam();
#if SERIAL && DEBUG_MEASURE
	Serial.print("MEM free ");
	Serial.println(payload.memfree);
#endif
#endif
}

// periodic report, i.e. send out a packet and optionally report on serial port
void doReport(void)
{
	rf12_sleep(RF12_WAKEUP);
	rf12_sendNow(0, &payload, sizeof payload);
	rf12_sendWait(RADIO_SYNC_MODE);
	rf12_sleep(RF12_SLEEP);
#if SERIAL
	Serial.print(node_id);
	Serial.print(" ");
#if LDR_PORT
	Serial.print((int) payload.light);
	Serial.print(' ');
#endif
#if PIR_PORT
	Serial.print((int) payload.moved);
	Serial.print(' ');
#endif
#if _DHT
	Serial.print((int) payload.rhum);
	Serial.print(' ');
	Serial.print((int) payload.temp);
	Serial.print(' ');
#endif
	Serial.print((int) payload.ctemp);
	Serial.print(' ');
#if _DS
	for (int i=0;i<ds_count;i++) {
		Serial.print((int) ds_value[i]);
		Serial.print(' ');
	}
#endif
#if _MEM
	Serial.print((int) payload.memfree);
	Serial.print(' ');
#endif
#if _RFM12LOBAT
	Serial.print((int) payload.lobat);
#endif
	Serial.println();
#endif // SERIAL || DEBUG
}

void runScheduler(char task)
{
	switch (task) {

		case ANNOUNCE:
			if (doAnnounce()) {
				scheduler.timer(MEASURE, 0);
			} else {
				scheduler.timer(ANNOUNCE, 100);
			}
			break;

		case MEASURE:
			// reschedule these measurements periodically
			scheduler.timer(MEASURE, MEASURE_PERIOD);
			doMeasure();
			// every so often, a report needs to be sent out
			if (++reportCount >= REPORT_EVERY) {
				reportCount = 0;
				scheduler.timer(REPORT, 0);
			}
			serialFlush();
			break;

		case REPORT:
			payload.msgtype = REPORT_MSG;
			doReport();
			serialFlush();
			break;

#if DEBUG
		default:
			Serial.print("0x");
			Serial.print(task, HEX);
			Serial.println(" ?");
			serialFlush();
			break;
#endif

	}
}

static void runCommand()
{
}

/* Main */

void setup(void)
{
#if SERIAL
	mpeser.begin();
	mpeser.startAnnounce(sketch, version);
	serialFlush();
#if DEBUG
	Serial.print(F("SRAM used: "));
	Serial.println(usedRam());
	byte rf12_show = 1;
#else
	byte rf12_show = 0;
#endif
#endif

	writeConfig();
	//doConfig();
	return;

	// Reported on serial?
	node_id = "CarrierCase.0-2-RF12-5-23";
	//<sketch>.0-<count>-RF12....
	rf12_id = 23;
	rf12_initialize(rf12_id, RF12_868MHZ, 5);
	//rf12_config(rf12_show);
    
	rf12_sleep(RF12_SLEEP); // power down

	// PIR pull down and interrupt 
	pir.digiWrite(0);
	// PCMSK2 = PCIE2 = PCINT16-23 = D0-D7
	bitSet(PCMSK2,  3 + PIR_PORT); // DIO1
	bitSet(PCICR, PCIE2); // enable PCMSK2 for PCINT at DIO1-4 (D4-7)

	/* warn out of bound if _EEPROMEX_DEBUG */
  //EEPROM.setMemPool(memBase, memCeiling);
  /* warn if _EEPROMEX_DEBUG */
  //EEPROM.setMaxAllowedWrites(maxAllowedWrites);

	/* read first config setting from memory, if empty start first-run init */

//	uint8_t nid = EEPROM.read(0);
//	if (nid == 0x0) {
//#if DEBUG
//		Serial.println("");
//#endif
		doConfig();
//	}
	Serial.println();
	serialFlush();

	initLibs();

	doReset();
}

void loop(void)
{
	//doMeasure();
	//doReport();
	//delay(15000);
	//return;
#if _DS
	bool ds_reset = digitalRead(7);
	if (ds_search || ds_reset) {
		if (ds_reset) {
			Serial.println("Reset triggered");
		}
		findDS18B20s();
		return;
	}
#endif

	debug_ticks();
	char task = scheduler.poll();
	if (task == 0xFF) {} // -1
	else if (task == 0xFE) {} // -2
	else runScheduler(task);

#if PIR_PORT
	if (pir.triggered()) {
		payload.moved = pir.state();
		doTrigger();
	}
#endif
}
