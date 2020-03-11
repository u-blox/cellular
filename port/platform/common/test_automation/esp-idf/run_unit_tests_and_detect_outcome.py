#!/usr/bin/env python
'''Run ESP-IDF unit tests and look for strings indicating failure.'''
import sys
import argparse
import serial
import re
import codecs

# Prefix to put at the start ofall prints
prompt = "RunEspIdfTests: "

# Flag indicating test completion
finished = False

# Various counters
reboots = 0
tests_run = 0
tests_failed = 0
tests_ignored = 0

def rebootCallback(match):
    '''Handler for reboots occuring unexpectedly'''
    global reboots
    print prompt + "progress update - target has rebooted!"
    reboots += 1
    finished = true

def runCallback(match):
    '''Handler for a test beginning to run'''
    global tests_run
    print prompt + "progress update - test " + match.group(1) + "() has started to run."
    tests_run += 1

def passCallback(match):
    '''Handler for a test passing'''
    print prompt + "progress update - test " + match.group(1) + "() has passed."

def failCallback(match):
    '''Handler for a test failing'''
    global tests_failed
    print prompt + "progress update - test " +  match.group(1) + "() has FAILED."
    tests_failed += 1

def finishCallback(match):
    '''Handler for a test run finishing'''
    global tests_run
    global tests_failed
    global tests_ignored
    global finished
    print prompt + "test run completed, " + match.group(1) + " test(s) run, " + \
          match.group(2) + " test(s) failed, " + match.group(3) + " tests(s) ignored."
    tests_run = int(match.group(1))
    tests_failed = int(match.group(2))
    tests_ignored = int(match.group(3))
    finished = true

# List of regex strings to look for in each line returned by
# the unit test output and a function to call when the regex
# is matched.  The regex result is passed to the callback.
# Regex tested at https://regex101.com/ selecting Python as the flavour
interesting = [[r"abort()", rebootCallback],
               # Match, for example "Running getSetMnoProfile..." capturing the "getSetMnoProfile" part
               [r"(?:^Running) +([^\.]+(?=\.))...$", runCallback],
               # Match, for example "C:/temp/file.c:890:connectedThings:PASS" capturing the "connectThings" part
               [r"(?:^.*?(?:\.c:))(?:[0-9]*:)(.*?):PASS$", passCallback],
               # Match, for example "C:/temp/file.c:900:tcpEchoAsync:FAIL:Function sock.  Expression Evaluated To FALSE" capturing the "connectThings" part
               [r"(?:^.*?(?:\.c:))(?:[0-9]*:)(.*?):FAIL:", failCallback],
               # Match, for example "22 Tests 1 Failures 0 Ignored" capturing the numbers
               [r"(^[0-9]+) Test(?:s*) ([0-9]+) Failure(?:s*) ([0-9]+) Ignored", finishCallback]]

# Read lines from port, returns the line as
# a string when terminator or '\n' is encountered.
# Does NOT return the terminating character
# If a read timeout occurs then None is returned.
def pwar_readline(port_handle, terminator=None):
    '''RunEspIdfTests: Phil Ware's marvellous readline function'''
    eol = False
    line = ""
    try:
        while not eol and line != None:
            buf = port_handle.read()
            if buf:
                character = buf.decode('ascii')
                eol = character == '\n'
                if not eol and terminator:
                    eol = character == terminator
                if not eol:
                    line = line + character
            else:
                line = None
    except serial.SerialException as ex:
        print prompt + str(type(ex).__name__) + " while accessing " \
              "port " + port_handle.name + ": " + str(ex.message) + "."
    except UnicodeDecodeError as ex:
        print prompt + str(type(ex).__name__) + " while decoding " \
              "data from port " + port_handle.name + ": " + str(ex.message) + \
              "this probably means the thing at the other end has crashed."
    return line

# Open the required COM port
def open_port(port_name):
    '''RunEspIdfTests: open serial port'''
    port_handle = None
    try:
        return_value = serial.Serial(port_name,
                                     115200,
                                     timeout=0.05)
        port_handle = return_value
    except (ValueError, serial.SerialException) as ex:
        print prompt + str(type(ex).__name__) + " while opening " \
              "serial port: " + str(ex.message) + "."
    return port_handle

# Run all the tests.
def run_all_tests(port_handle, file_handle):
    '''RunEspIdfTests: run all of the tests'''
    success = False
    try:
        # First, flush any rubbish out of the COM port
        # otherwise we may get unicode decoding errors
        port_handle.read(128)
        line = ""
        # Read the opening splurge from the target
        while line.find("Press ENTER to see the list of tests.") < 0:
            line = pwar_readline(port_handle, '\r')
            if file_handle != None and line != None:
                file_handle.write(line + "\n")
        # For debug purposes, send a newline to the unit
        # test app to get it to list the tests
        print prompt + "listing tests."
        port_handle.write("\r\n".encode("ascii"))
        line = ""
        while line != None:
            line = pwar_readline(port_handle, '\r')
            if file_handle != None and line != None:
                file_handle.write(line + "\n")
        # Now send a * in to run all of the tests
        port_handle.write("*\r\n".encode("ascii"))
        print prompt + "running all tests."
        success = True
    except serial.SerialException as ex:
        print prompt + str(type(ex).__name__) + " while accessing " \
              "unit test port: " + str(ex.message) + "."
    return success

# Watch the output from the tests being run
# looking for interesting things
def watch_tests(port_handle, file_handle):
    '''RunEspIdfTests: watch test output'''
    print prompt + "watching test output until test run completes."
    while not finished:
        line = pwar_readline(port_handle, '\r')
        if line != None:
            if file_handle != None:
                file_handle.write(line + "\n")
            for entry in interesting:
                match = re.match(entry[0], line)
                if match:
                    entry[1](match)
            
if __name__ == "__main__":
    '''RunEspIdfTests: main'''
    success = True

    # Switch off traceback to stop the horrid developmenty prints
    #sys.tracebacklimit = 0
    parser = argparse.ArgumentParser(description="A script to " \
                                     "run ESP-IDF unit tests and " \
                                     "detect the outcome, communicating" \
                                     "with the ESP32 target running" \
                                     "a unit test build over a COM port.")
    parser.add_argument("port_name", metavar='p', help= \
                        "the COM port on which the unit test build" \
                        "is communicating, e.g. COM1; the baud rate" \
                        "is fixed at 115,200.")
    parser.add_argument("file_name", metavar='f', help= \
                        "the file name to write test output to;" \
                        "any existing file will be overwritten.")
    args = parser.parse_args()

    # The following line works around weird encoding problems where Python
    # doesn't like code page 65001 character encoding which Windows does
    # See https://stackoverflow.com/questions/878972/windows-cmd-encoding-change-causes-python-crash
    codecs.register(lambda name: codecs.lookup('utf-8') if name == 'cp65001' else None)

    # Do the work
    port_handle = open_port(args.port_name)
    if port_handle != None:
        if args.file_name:
            file_handle = open(args.file_name, "w")
            if not file_handle:
                success = False
                print prompt + "unable to open file " + args.file_name + " for writing."
        if success:
            if run_all_tests(port_handle, file_handle):
                watch_tests(port_handle, file_handle)
        if file_handle:
            file_handle.close()
        close(port_handle)
    print prompt + "end."
