Repeat - a command line utility
===============================

This command line program will execute a command repeatedly.  It is intended to replace the shell pattern:

    while something; do sleep $seconds; done

With options, it is considerably more flexible.

Options
-------

* `--interval` *duration* - Specifies an interval between invocations.  Defaults to 0.
* `--times` *num* - Executes for a maximum number of times, then exit.
* `--untilerr` - Stops repeating when the command's exit code is non-zero
* `--untilsuccess` - Stops repeating when the command's exit code is zero
* `--precise` - Runs command at specified intervals instead of waiting the interval between executions.
* `--noshell` - Runs command directly instead of via an intermediate shell
* `--help` - Display usage and exit
* `--version` - Display version info and exit

Examples
--------

* `repeat echo Hello World`
    Prints out Hello World forever
* `repeat -n 5 echo Hello World`
    Prints out Hello World five times
* `repeat -i 1 echo Hello World`
    Prints out Hello World with a second between each invocation
* `repeat -i 1 -e -p -t 5 echo Hello World`
    Prints out Hello World five times, once a second, stopping if echo returns an error.
* `repeat -i 5 -s grep foobar myfile`
    Checks every seconds for foobar in myfile until it succeeds.
