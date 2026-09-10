
# Logging

Yarr uses spdlog for logging. This is used to enable turning on/off output
from different parts of the system dynamically.

Each logger provides output with specific level:

* trace
* debug
* info
* warn
* error
* critical

The default for scanConsole is to enable output of message of info level or
higher from all loggers. The loggers can be configured, using a json file,
by passing the -l flag to scanConsole.

The same configuration can be used for investigating test failures passing 
--logger-config FILENAME to test_main.

You can list all available loggers (that can be enabled/disabled) as part of:

``scanConsole -k``

## Default configuration

For most applications the same default is used (report all info level messages).
Additionally, the file `configs/logging/default.json` (NB relative to current
directory) is loaded.

For several other applications, a custom configuration is used (for instance to
only output from specific loggers).

In both cases, a default format pattern is used.

## Logger configuration

Most units in YARR create a logger with a particular name and send messages
to it. The configuration file describes how these messages are dealt with.

In general the steps are:

1. Generate message with associated logger and level
2. Check message against level configured for the logger
3. For each configured sink
   1. Check message against level configured for the sink
   2. Send message to sink, which formats it using the format pattern.

It is important to note that all three of the message, the logger and the
sink have an associated level. The message will be discarded if it's below
either the logger or sink level.

For examples see the files in: configs/logging.

### Configuration file

The configuration file is based on json and should allow flexible
configuration of the log levels and sinks.

The top level properties "pattern" and "level" are applied to the 
default sink, which outputs to the console. See the spdlog 
documentation for the format of the pattern.

The sinks property is an array of dictionaries, which are used to
configure additional sinks (for instance writing to file). Each item
contains a "name" field which is used to specify a key that can be
used later. The "file_name" property specifies the output file name.
The "level" and "pattern" properties are the same as for the default
sink.

For debugging logger files, the "report_loggers" flag can be set
which reports information on the logger and sink setup.

### Per-logger configuration

The "log_config" property is an array of dictionaries used to set the level
and/or sink of individual loggers (as opposed to "level"/"pattern" at the
top level, which only affect the default console sink). Each item has:

* "name": the logger name to configure (as reported by ``scanConsole -k``),
  or the special value "all" to apply to every registered logger.
* "level" (optional): the level to set for this logger.
* "sink" (optional): the name of a sink defined in the "sinks" array to
  route this logger's output to, in addition to the default console sink.
  If omitted, the default sink is used.

For example, to trace messages from just the ``SpecRx`` logger:

```json
{
    "pattern": "[%n] %! %v",
    "log_config": [
        { "name": "SpecRx", "level": "trace" }
    ]
}
```

Or to send all loggers' output to a file in addition to the console:

```json
{
    "sinks": [
        { "name": "file", "level": "info", "file_name": "console.log" }
    ],
    "log_config": [
        { "name": "all", "sink": "file" }
    ]
}
```

See `configs/logging/` for further worked examples (`trace_spec.json`,
`default_to_file.json`, `trace_all.json`, etc).
