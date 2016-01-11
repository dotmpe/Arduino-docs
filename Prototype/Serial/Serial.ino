/*

Serial extends AtmegaEEPROM, AtmegaTemp

  - boilerplate for ATmega node with Serial interface
  - uses Node event loop which is based on JeeLib scheduler.


 */



/* *** Globals and sketch configuration *** */
#define SERIAL          1 /* Enable serial */
#define DEBUG           0 /* Enable trace statements */

#define _MEM            1

#define UI_IDLE         4000  // tenths of seconds idle time, ...
#define UI_STDBY        8000  // ms
#define MAXLENLINE      79
#define CONFIG_VERSION "nx1"
#define CONFIG_START 0




// Definition of interrupt names
//#include <avr/io.h>
// ISR interrupt service routine
//#include <avr/interrupt.h>

//#include <SoftwareSerial.h>
//#include <EEPROM.h>
//#include <util/crc16.h>
#include <JeeLib.h>
#include <DotmpeLib.h>
#include <mpelib.h>




const String sketch = "X-Serial";
const int version = 0;

char node[] = "rf24n";

volatile bool ui_irq;
bool ui;


/* IO pins */
//              RXD      0
//              TXD      1
//              INT0     2     UI IRQ
#       define BL       10 // PWM Backlight
//              MOSI     11
//              MISO     12
#       define _DBG_LED 13 // SCK
//#       define          A0
//#       define          A1
//#       define          A2
//#       define          A3
//#       define          A4
//#       define          A5


MpeSerial mpeser (57600);

MilliTimer idle, stdby;

/* *** InputParser *** {{{ */

Stream& cmdIo = Serial;
extern InputParser::Commands cmdTab[];
InputParser parser (50, cmdTab);


/* *** /InputParser }}} *** */

/* *** Report variables *** {{{ */




/* *** /Report variables }}} *** */

/* *** Scheduled tasks *** {{{ */

enum {
	ANNOUNCE,
	TASK_END
};
// Scheduler.pollWaiting returns -1 or -2
static const char SCHED_WAITING = 0xFF; // -1: waiting to run
static const char SCHED_IDLE = 0xFE; // -2: no tasks running

static word schedbuf[TASK_END];
Scheduler scheduler (schedbuf, TASK_END);

// has to be defined because we're using the watchdog for low-power waiting
ISR(WDT_vect) { Sleepy::watchdogEvent(); }


/* *** /Scheduled tasks *** }}} */

/* *** EEPROM config *** {{{ */

// See Prototype Node or SensorNode

struct Config {
	char node[4];
	int node_id;
	int version;
	char config_id[4];
	int temp_offset;
} static_config = {
	/* default values */
	{ node[0], node[1], 0, 0 }, 0, version,
	CONFIG_VERSION, TEMP_OFFSET
};

Config config;

bool loadConfig(Config &c)
{
  return false;
}

void writeConfig(Config &c)
{
}



/* *** /EEPROM config *** }}} */

/* *** Peripheral devices *** {{{ */


/* *** /Peripheral devices *** }}} */

/* *** Peripheral hardware routines *** {{{ */





#if _NRF24
/* Nordic nRF24L01+ radio routines */


#endif // NRF24 funcs

#if _RTC
#endif //_RTC

#if _HMC5883L
/* Digital magnetometer I2C routines */


#endif // HMC5883L



/* *** /Peripheral hardware routines *** }}} */

/* *** UI *** {{{ */


//ISR(INT0_vect)
void irq0()
{
	ui_irq = true;
	//Sleepy::watchdogInterrupts(0);
}

/* *** /UI *** }}} */

/* UART commands {{{ */

static void ser_helpCmd(void) {
	cmdIo.println("n: print Node ID");
//	Serial.println("c: print config");
//	Serial.println("v: print version");
	Serial.println("m: print free and used memory");
//	Serial.println("N: set Node (3 byte char)");
//	Serial.println("C: set Node ID (1 byte int)");
//	Serial.println("W: load/save EEPROM");
//	Serial.println("E: erase EEPROM!");
//	Serial.println("?/h: this help");
	idle.set(UI_IDLE);
}

static void ser_configNodeCmd(void) {
	//Serial.println("n " + node_id);
	//Serial.print('n');
	//Serial.print(' ');
	//Serial.print(node_id);
	//Serial.print('\n');
}

static void ser_configVersionCmd(void) {
	//Serial.print("v ");
	//showNibble(static_config.version);
	//Serial.println("");
}

void memStat() {
	int free = freeRam();
	int used = usedRam();

	cmdIo.print("m ");
	cmdIo.print(free);
	cmdIo.print(' ');
	cmdIo.print(used);
	cmdIo.print(' ');
	cmdIo.println();
}

void ser_tempOffset(void) {
}

void ser_temp(void) {
  Serial.println(internalTemp() + config.temp_offset);
}

static void ser_test_cmd(void) {
	Serial.println("Foo");
}

void valueCmd () {
	int v;
	parser >> v;
	Serial.print("value = ");
	Serial.println(v);
	idle.set(UI_IDLE);
}

// forward declarations
void doReport(void);
void doMeasure(void);

void reportCmd () {
	doReport();
	idle.set(UI_IDLE);
}

void measureCmd() {
	doMeasure();
	idle.set(UI_IDLE);
}

void stdbyCmd() {
	ui = false;
}
#endif


/* UART commands }}} */

/* *** Initialization routines *** {{{ */

void initConfig(void)
{
	// See Prototype/Node
	sprintf(node_id, "%s%i", static_config.node, static_config.node_id);
}

void doConfig(void)
{
	/* load valid config or reset default config */
	if (!loadConfig(static_config)) {
		writeConfig(static_config);
	}
	initConfig();
}

void initLibs()
{
}


/* *** /Initialization routines *** }}} */

/* *** Run-time handlers *** {{{ */

void doReset(void)
{
	//doConfig();
	loadConfig(static_config);
	initConfig();

#ifdef _DBG_LED
	pinMode(_DBG_LED, OUTPUT);
#endif
	attachInterrupt(INT0, irq0, RISING);
	ui_irq = true;
	tick = 0;

	scheduler.timer(ANNOUNCE, 0);
}

bool doAnnounce(void)
{

	cmdIo.print("\n[");
	cmdIo.print(sketch);
	cmdIo.print(".");
	cmdIo.print(version);
	cmdIo.println("]");

	cmdIo.println(node_id);
	return false;
}


// readout all the sensors and other values
void doMeasure()
{
	// none, see  SensorNode
}

// periodic report, i.e. send out a packet and optionally report on serial port
void doReport(void)
{
	// none, see RadioNode
}

void uiStart()
{
	idle.set(UI_IDLE);
	if (!ui) {
		ui = true;
	}
}

void runScheduler(char task)
{
	switch (task) {

		case ANNOUNCE:
				doAnnounce();
				//scheduler.timer(ANNOUNCE, 100);
			serialFlush();
			break;


#if DEBUG && SERIAL
		case SCHED_WAITING:
		case SCHED_IDLE:
			Serial.print("!");
			serialFlush();
			break;

		default:
			Serial.print("0x");
			Serial.print(task, HEX);
			Serial.println(" ?");
			serialFlush();
			break;
#endif

	}
}


/* *** /Run-time handlers *** }}} */

/* *** InputParser handlers *** {{{ */

// See Node for proper cmds, basic examples here
#if SERIAL

InputParser::Commands cmdTab[] = {
	{ '?', 0, ser_helpCmd },
	{ 'h', 0, ser_helpCmd },
	{ 'v', 2, valueCmd },
	{ 'm', 0, memStat},
	{ 'M', 0, measureCmd },
	{ 'r', 0, reportCmd },
	{ 's', 0, stdbyCmd },
	{ 'x', 0, doReset },
	{ 't', 0, ser_test_cmd },
	{ 0 }
};

#endif // SERIAL


/* *** /InputParser handlers *** }}} */

/* *** Main *** {{{ */


void setup(void)
{
#if SERIAL
	mpeser.begin();
	mpeser.startAnnounce(sketch, String(version));
#if DEBUG || _MEM
	Serial.print(F("Free RAM: "));
	Serial.println(freeRam());
#endif
	serialFlush();
#endif

	initLibs();

	doReset();
}

void loop(void)
{
#ifdef _DBG_LED
	blink(_DBG_LED, 3, 10);
#endif
	if (ui_irq) {
		debugline("Irq");
		ui_irq = false;
		uiStart();
		//analogWrite(BL, 0xAF ^ BL_INVERTED);
	}
	debug_ticks();

	if (cmdIo.available()) {
		parser.poll();
		return;
	}

	char task = scheduler.poll();
	if (-1 < task && task < SCHED_IDLE) {
		runScheduler(task);
	}

	if (ui) {
		if (idle.poll()) {
			stdby.set(UI_STDBY);
		} else if (stdby.poll()) {
			ui = false;
		} else if (!stdby.idle()) {
			// XXX toggle UI stdby Power, got to some lower power mode..
			delay(30);
		}
	} else {
#ifdef _DBG_LED
		blink(_DBG_LED, 1, 25);
#endif
		serialFlush();
		char task = scheduler.pollWaiting();
		if (-1 < task && task < SCHED_IDLE) {
			runScheduler(task);
		}
	}
}

/* *** }}} */

