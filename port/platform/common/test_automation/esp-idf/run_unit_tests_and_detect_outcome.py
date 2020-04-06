#!/usr/bin/env python
'''Run ESP-IDF unit tests and look for strings indicating failure.'''
import sys
import argparse
import serial
import re
import codecs
from time import time, ctime
from math import ceil

# Prefix to put at the start ofall prints
prompt = "RunEspIdfTests: "

# Flag indicating test completion
finished = False

# Various counters
reboots = 0
tests_run = 0
tests_failed = 0
tests_ignored = 0
overall_start_time = 0
last_start_time = 0
test_outcomes = [] # Name, duration in seconds and status string

def reboot_callback(match):
    '''Handler for reboots occuring unexpectedly'''
    global reboots
    print prompt + "progress update - target has rebooted!"
    reboots += 1
    finished = True

def run_callback(match):
    '''Handler for a test beginning to run'''
    global tests_run
    global last_start_time
    last_start_time = time()
    print "{}progress update - test {}() started on {}.".\
          format(prompt, match.group(1), ctime(last_start_time))

    tests_run += 1

def record_test_outcome(name, duration, status):
    outcome = [name, duration, status]
    test_outcomes.append(outcome)

def pass_callback(match):
    '''Handler for a test passing'''
    global last_start_time
    end_time = time()
    duration = int(ceil(end_time - last_start_time))
    print "{}progress update - test {}() passed on {} after running for {:.0f} second(s).".\
          format(prompt, match.group(1), ctime(end_time), ceil(end_time - last_start_time))
    record_test_outcome(match.group(1), int(ceil(end_time - last_start_time)), "PASS")

def fail_callback(match):
    '''Handler for a test failing'''
    global tests_failed
    global last_start_time
    tests_failed += 1
    end_time = time()
    print "{}progress update - test {}() FAILED at {} after running for {:.0f} second(s).".\
          format(prompt, match.group(1), ctime(end_time), ceil(end_time - last_start_time))
    record_test_outcome(match.group(1), int(ceil(end_time - last_start_time)), "FAIL")

def finish_callback(match):
    '''Handler for a test run finishing'''
    global tests_run
    global tests_failed
    global tests_ignored
    global finished
    global overall_start_time
    end_time = time()
    duration_hours = int((end_time - overall_start_time) / 3600)
    duration_minutes = int(((end_time - overall_start_time) - (duration_hours * 3600)) / 60)
    duration_seconds = int((end_time - overall_start_time) - (duration_hours * 3600) - (duration_minutes * 60))
    tests_run = int(match.group(1))
    tests_failed = int(match.group(2))
    tests_ignored = int(match.group(3))
    print "{}test run completed on {}, {} test(s) run, {} test(s) failed, {} test(s) ignored, test run took {}:{:02d}:{:02d}.". \
          format(prompt, ctime(end_time),tests_run, tests_failed, tests_ignored, \
                 duration_hours, duration_minutes, duration_seconds)

    finished = True

# List of regex strings to look for in each line returned by
# the unit test output and a function to call when the regex
# is matched.  The regex result is passed to the callback.
# Regex tested at https://regex101.com/ selecting Python as the flavour
interesting = [[r"abort()", reboot_callback],
               # Match, for example "Running getSetMnoProfile..." capturing the "getSetMnoProfile" part
               [r"(?:^Running) +([^\.]+(?=\.))...$", run_callback],
               # Match, for example "C:/temp/file.c:890:connectedThings:PASS" capturing the "connectThings" part
               [r"(?:^.*?(?:\.c:))(?:[0-9]*:)(.*?):PASS$", pass_callback],
               # Match, for example "C:/temp/file.c:900:tcpEchoAsync:FAIL:Function sock.  Expression Evaluated To FALSE" capturing the "connectThings" part
               [r"(?:^.*?(?:\.c:))(?:[0-9]*:)(.*?):FAIL:", fail_callback],
               # Match, for example "22 Tests 1 Failures 0 Ignored" capturing the numbers
               [r"(^[0-9]+) Test(?:s*) ([0-9]+) Failure(?:s*) ([0-9]+) Ignored", finish_callback]]

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
def run_all_tests(port_handle, log_file_handle):
    '''RunEspIdfTests: run all of the tests'''
    global overall_start_time
    success = False
    try:
        # First, flush any rubbish out of the COM port
        # otherwise we may get unicode decoding errors
        port_handle.read(128)
        line = ""
        # Read the opening splurge from the target
        # if there is any
        while line != None and \
              line.find("Press ENTER to see the list of tests.") < 0:
            line = pwar_readline(port_handle, '\r')
            if log_file_handle != None and line != None:
                log_file_handle.write(line + "\n")
        # For debug purposes, send a newline to the unit
        # test app to get it to list the tests
        print prompt + "listing tests."
        port_handle.write("\r\n".encode("ascii"))
        line = ""
        while line != None:
            line = pwar_readline(port_handle, '\r')
            if log_file_handle != None and line != None:
                log_file_handle.write(line + "\n")
        # Now send a * in to run all of the tests
        port_handle.write("*\r\n".encode("ascii"))
        overall_start_time = time()
        print prompt + "run of all tests started on " + ctime(overall_start_time) + "."
        success = True
    except serial.SerialException as ex:
        print prompt + str(type(ex).__name__) + " while accessing " \
              "unit test port: " + str(ex.message) + "."
    return success

# Watch the output from the tests being run
# looking for interesting things
def watch_tests(port_handle, log_file_handle):
    '''RunEspIdfTests: watch test output'''
    print prompt + "watching test output until test run completes."
    while not finished:
        line = pwar_readline(port_handle, '\r')
        if line != None:
            if log_file_handle != None:
                log_file_handle.write(line + "\n")
            for entry in interesting:
                match = re.match(entry[0], line)
                if match:
                    entry[1](match)
            
if __name__ == "__main__":
    '''RunEspIdfTests: main'''
    success = True
    return_value = 1

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
    parser.add_argument("log_file_name", metavar='l', help= \
                        "the file name to write test output to;" \
                        "any existing file will be overwritten.")
    parser.add_argument("report_file_name", metavar='r', help= \
                        "the file name to write an XML-format report to;" \
                        "any existing file will be overwritten.")
    args = parser.parse_args()

    # The following line works around weird encoding problems where Python
    # doesn't like code page 65001 character encoding which Windows does
    # See https://stackoverflow.com/questions/878972/windows-cmd-encoding-change-causes-python-crash
    codecs.register(lambda name: codecs.lookup('utf-8') if name == 'cp65001' else None)

    # Do the work
    port_handle = open_port(args.port_name)
    if port_handle != None:
        if args.log_file_name:
            log_file_handle = open(args.log_file_name, "w")
            if not log_file_handle:
                success = False
                print prompt + "unable to open file " + args.log_file_name + " for writing."
        if args.report_file_name:
            report_file_handle = open(args.report_file_name, "w")
            if not report_file_handle:
                success = False
                print prompt + "unable to open file " + args.report_file_name + " for writing."
        if success:
            # Run the tests
            if run_all_tests(port_handle, log_file_handle):
                watch_tests(port_handle, log_file_handle)
                return_value = tests_failed
            # Write the report
            if report_file_handle:
                report_file_handle.write("<testsuite name=\"{}\" tests=\"{}\" failures=\"{}\">\n".\
                                         format("esp-idf", tests_run, tests_failed))
                for outcome in test_outcomes:
                    report_file_handle.write("    <testcase classname=\"{}\" name=\"{}\" time=\"{}\" status=\"{}\"></testcase>\n".\
                                             format("cellular_tests", outcome[0], outcome[1], outcome[2]))
                report_file_handle.write("</testsuite>\n")
                report_file_handle.close()
        if log_file_handle:
            log_file_handle.close()
        port_handle.close()
    print prompt + "end with return value " + str(return_value) + "."
    sys.exit(return_value)
