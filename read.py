#!/usr/bin/env python
"""
read - Serial line reader for JeeNode/Arduino.

Keeps data in /tmp/ directory.
Deprecated by readhex which reads from JeeNode with rf12demo or Mpe/RadioLink.
"""
import os, sys, time
if sys.platform == 'win32':
	from twisted.internet import win32eventreactor
	win32eventreactor.install()

from twisted.python import log, usage
from twisted.internet import reactor
from twisted.protocols.basic import LineReceiver
import serial


sketches = {
		'ROOM': 0,
		'MOIST': 0
	}
nodes = {}

def path(node_id, sensor_id):
	"""
	Return a temporary path to share data with Munin.
	Each value has its own path, based on node ID and sensor ID.
	"""
	path = os.path.join('/tmp/martador', node_id)
	if not os.path.exists(path):
		os.makedirs(path)
	return os.path.join('/tmp/martador', node_id, sensor_id)

class Martador(LineReceiver):

	def processData(self, *args):
		data = list(args)
		if data:
			key = data.pop(0)
		else:
			return
		if key == 'REG':
			# register sketch, note the types it reports
			sketches[data[0]] += 1
			node_id = "%s-%s" % ( data[0], str(sketches[data[0]]) )
			report_types = data[1:]
			#for i, type_ in enumerate(report_types):
			#	report_types[i] = type_
			if node_id in nodes:
				return
			nodes[node_id] = report_types
			time.sleep(1)
			self.sendLine('NEW %s\r' % node_id)
			time.sleep(1)
			self.sendLine('')
			print 'NEW %s' % node_id
			time.sleep(1)
			return
		elif key == 'SEN':
			# let the node give paremeters for one of its sensors
			pass
		elif key in nodes:
			for i, value in enumerate(data):
				print path(key, nodes[key][i]), 'w+', "%s" % value
				open(path(key, nodes[key][i]), 'w+').write("%s" % value)
		else:
			print args

	def lineReceived(self, line):
		try:
			data = line.split()
			self.processData(*data)
		except ValueError:
			logging.error('Unable to parse data %s' % line)
			return


class MartadorOptions(usage.Options):
	optFlags = [
	]
	optParameters = [
		['outfile', 'o', None, 'Logfile [default: sys.stdout]'],
		['baudrate', 'b', 57600, 'Serial baudrate'],
		['port', 'p', '/dev/ttyUSB0', 'Serial Port device'],
	]


if __name__ == '__main__':

	from twisted.internet import reactor
	from twisted.internet.serialport import SerialPort


	o = MartadorOptions()
	try:
		o.parseOptions()
	except usage.UsageError, errortext:
		print '%s: %s' % (sys.argv[0], errortext)
		print '%s: Try --help for usage details.' % (sys.argv[0])
		raise SystemExit, 1

	logFile = o.opts['outfile']
	if logFile is None:
		logFile = sys.stdout
	log.startLogging(logFile)

	if o.opts['baudrate']:
		baudrate = int(o.opts['baudrate'])

	serial.Serial(port=None, baudrate=baudrate, bytesize=8, parity='N', stopbits=1,
			timeout=None, xonxoff=0, rtscts=0, writeTimeout=None, dsrdtr=None)

	log.msg('Attempting to open %s at %dbps' % (o.opts['port'], baudrate))
	s = SerialPort(Martador(), o.opts['port'], reactor, baudrate=baudrate)
	log.msg("Running")
	reactor.run()

# vim:noet:
