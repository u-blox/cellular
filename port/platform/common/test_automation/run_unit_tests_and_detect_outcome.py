#!/usr/bin/env python
'''Run ESP-IDF unit tests and look for strings indicating failure.'''
import sys
import argparse
import serial
import re
import codecs
import subprocess
from time import time, ctime, sleep
from math import ceil
import socket
from telnetlib import Telnet # For talking to JLink server, NRF52840

# Prefix to put at the start ofall prints
prompt = "RunEspIdfTests: "

# Flag indicating test completion
finished = False

# The connection types
CONNECTION_NONE = 0
CONNECTION_SERIAL = 1
CONNECTION_TELNET = 2
CONNECTION_EXE = 3

# Various counters
reboots = 0
tests_run = 0
tests_failed = 0
tests_ignored = 0
overall_start_time = 0
last_start_time = time()
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
    print "{}progress update - test {}() FAILED on {} after running for {:.0f} second(s).".\
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
               # Match, for example "BLAH: Running getSetMnoProfile..." capturing the "getSetMnoProfile" part
               [r"(?:^.*Running) +([^\.]+(?=\.))...$", run_callback],
               # Match, for example "C:/temp/file.c:890:connectedThings:PASS" capturing the "connectThings" part
               [r"(?:^.*?(?:\.c:))(?:[0-9]*:)(.*?):PASS$", pass_callback],
               # Match, for example "C:/temp/file.c:900:tcpEchoAsync:FAIL:Function sock.  Expression Evaluated To FALSE" capturing the "connectThings" part
               [r"(?:^.*?(?:\.c:))(?:[0-9]*:)(.*?):FAIL:", fail_callback],
               # Match, for example "22 Tests 1 Failures 0 Ignored" capturing the numbers
               [r"(^[0-9]+) Test(?:s*) ([0-9]+) Failure(?:s*) ([0-9]+) Ignored", finish_callback]]

# Read lines from input, returns the line as
# a string when terminator or '\n' is encountered.
# Does NOT return the terminating character
# If a read timeout occurs then None is returned.
def pwar_readline(in_handle, connection_type, terminator = None):
    '''RunEspIdfTests: Phil Ware's marvellous readline function'''
    line = ""
    if connection_type == CONNECTION_TELNET:
        # I was hoping that all sources of data
        # would have a read() function but it turns
        # out that Telnet does not, it has read_until()
        # which returns a whole line with a timeout
        # Note: deliberately don't handle the exception that
        # the Telnet port has been closed here, allow it
        # to stop us entirely
        if terminator == None:
            terminator = '\n'
        # Long time-out as we don't want partial lines
        buf = in_handle.read_until(terminator, 1)
        if buf != "":
            line = buf.decode('ascii')
            # To make this work the same was as the
            # serial and exe cases, need to remove the terminator
            # and remove any dangling \n left on the front
            line = line.rstrip(terminator)
            line = line.lstrip('\n')
        else:
            line = None
        return line
        # Serial ports and exe's just use read()
    else:
        if (connection_type == CONNECTION_SERIAL) or \
           (connection_type == CONNECTION_EXE):
            eol = False
            try:
                while not eol and line != None:
                    buf = in_handle.read(1)
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
                      "port" + in_handle.name + ": " + str(ex.message) + "."
            except UnicodeDecodeError as ex:
                print prompt + str(type(ex).__name__) + " while decoding " \
                      "data from " + in_handle.name + ": " + str(ex.message) + \
                      "this probably means the thing at the other end has crashed."
            return line
        else:
            return None

# Open the required serial port.
def open_serial(serial_name):
    '''RunEspIdfTests: open serial port'''
    serial_handle = None
    print prompt + "trying to open \"" + serial_name + "\" as a serial port...",
    try:
        return_value = serial.Serial(serial_name,
                                     115200,
                                     timeout = 0.05)
        serial_handle = return_value
        print " opened."
    except (ValueError, serial.SerialException) as ex:
        print " failed."
    return serial_handle

# Open the required telnet port.
def open_telnet(port_number):
    '''RunEspIdfTests: open telnet port on localhost'''
    telnet_handle = None
    print prompt + "trying to open \"" + port_number + \
          "\" as a telnet port on localhost...",
    try:
        telnet_handle = Telnet('localhost',
                               int(port_number),
                               timeout = 1)
        if telnet_handle != None:
            print " opened."
        else:
            print " failed."
    except (socket.timeout) as ex:
        print " failed."
    return telnet_handle

# Start the required executable.
def start_exe(exe_name):
    '''RunEspIdfTests: launch an executable as a sub-process'''
    print prompt + "trying to launch \"" + exe_name + "\" as an executable...",
    return_value = None
    stdout_handle = None
    try:
        return_value = subprocess.Popen(exe_name,
                                        stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE,
                                        shell = True,
                                        bufsize = -1)
        sleep(5)
        # A return value of any sort indicates that the process exited
        if return_value.poll() != None:
            print " failed."
        else:
            stdout_handle = return_value.stdout
            if stdout_handle != None:
                print " launched."
            else:
                print " launched but output stream returned was NULL so failed."
    except (ValueError, serial.SerialException) as ex:
        print " failed: " + str(type(ex).__name__) + " while " \
              "trying to execute " + str(ex.message) + "."
    return return_value, stdout_handle

# Run all the tests.
def run_all_tests(in_handle, connection_type, log_file_handle):
    '''RunEspIdfTests: run all of the tests'''
    global overall_start_time
    success = False
    try:
        line = ""
        # Read the opening splurge from the target
        # if there is any
        print prompt + "reading initial text from input..."
        while line != None and \
              line.find("Press ENTER to see the list of tests.") < 0:
            line = pwar_readline(in_handle, connection_type, '\r')
            if log_file_handle != None and line != None:
                log_file_handle.write(line + "\n")
        # For debug purposes, send a newline to the unit
        # test app to get it to list the tests
        print prompt + "listing tests..."
        in_handle.write("\r\n".encode("ascii"))
        line = ""
        while line != None:
            line = pwar_readline(in_handle, connection_type, '\r')
            if log_file_handle != None and line != None:
                log_file_handle.write(line + "\n")
        # Now send a * in to run all of the tests
        print prompt + "sending command to run all tests..."
        in_handle.write("*\r\n".encode("ascii"))
        overall_start_time = time()
        print prompt + "run of all tests started on " + ctime(overall_start_time) + "."
        success = True
    except serial.SerialException as ex:
        print prompt + str(type(ex).__name__) + " while accessing " \
              "unit test port: " + str(ex.message) + "."
    return success

# Watch the output from the tests being run
# looking for interesting things.
def watch_tests(in_handle, connection_type, log_file_handle):
    '''RunEspIdfTests: watch test output'''
    print prompt + "watching test output until test run completes."
    while not finished:
        line = pwar_readline(in_handle, connection_type, '\r')
        if line != None:
            if log_file_handle != None:
                log_file_handle.write(line + "\n")
                log_file_handle.flush()
            for entry in interesting:
                match = re.match(entry[0], line)
                if match:
                    entry[1](match)
            
if __name__ == "__main__":
    '''RunEspIdfTests: main'''
    success = True
    exe_handle = None
    return_value = 1
    connection_type = CONNECTION_NONE

    # Switch off traceback to stop the horrid developmenty prints
    #sys.tracebacklimit = 0
    parser = argparse.ArgumentParser(description="A script to " \
                                     "run ESP-IDF unit tests and " \
                                     "detect the outcome, communicating" \
                                     "with the ESP32 target running" \
                                     "a unit test build.")
    parser.add_argument("input_name", metavar='p', help= \
                        "the source of data: either the COM port on" \
                        "which the unit test build is communicating," \
                        "e.g. COM1 (baud rate fixed at 115,200)" \
                        "or a port number (in which case a Telnet" \
                        "session is opened on localhost to grab the" \
                        "output) or an executable to run which should" \
                        "spew  the output from the unit test.")
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
    if args.input_name:
        connection_type = CONNECTION_SERIAL
        in_handle = open_serial(args.input_name)
        if in_handle == None:
            connection_type = CONNECTION_TELNET
            in_handle = open_telnet(args.input_name)
        if in_handle == None:
            connection_type = CONNECTION_EXE
            exe_handle, in_handle = start_exe(args.input_name)
        if in_handle != None:
            if args.log_file_name:
                log_file_handle = open(args.log_file_name, "w")
                if log_file_handle:
                    print prompt + "writing log output to \"" + args.log_file_name + "\"."
                else:
                    success = False
                    print prompt + "unable to open log file \"" + args.log_file_name + "\" for writing."
            if args.report_file_name:
                report_file_handle = open(args.report_file_name, "w")
                if report_file_handle:
                    print prompt + "writing report to \"" + args.report_file_name + "\"."
                else:
                    success = False
                    print prompt + "unable to open report file \"" + args.report_file_name + "\" for writing."
            if success:
                # Run the tests
                # If we have a serial or telnet the we have
                # bi-directional comms and need to chose
                # which tests to run, else the lot will
                # just run
                if exe_handle or run_all_tests(in_handle, connection_type, log_file_handle):
                    watch_tests(in_handle, connection_type, log_file_handle)
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
            if exe_handle:
                exe_handle.terminate()
            else:
                in_handle.close()
    print prompt + "end with return value " + str(return_value) + "."
    sys.exit(return_value)
