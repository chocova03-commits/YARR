
# Logging

Yarr uses spdlog for logging. This is used to enable turning on/off output
from different parts of the system dynamically.

Named loggers are used to send messages with different levels: trace, debug,
info, warning, error and critical.

The default for scanConsole is to enable output of message of info level or
higher from all loggers. The loggers can be configured, using a json file,
by passing the -l flag to scanConsole.

Each logger provides output with specific level which should be used for
different purposes:

* `trace`: for very low level messages
* `debug`: messages generally useful for debugging (normally one, or a few, logger would be enabled at a time)
* `info`: reporting control flow in general operation
* `warning`: something the user should be aware about, but the program can continue and should be able to complete successfully
* `error`: something happened that could lead to unintended consequences, but the program can continue
* `critical`: something happened that will lead to unintended consequences, the program cannot continue (normally followed by `abort()`)

The same configuration file can be used for investigating unit-test failures by
passing --logger-config FILENAME to testYarr et al.

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

## Trace logging

In many cases the `trace` level can cause performance issues. Thus the
whole invocation can wrapped in a macro which is not enabled in the 
default configuration.

If you want to see these you'll need to configure cmake with:

```bash
cmake ... -DYARR_ACTIVE_LOGGER_LEVEL=TRACE
```

in addition to enabling the trace level using the runtime configuration.

This is not the case for all trace messages, 

```cpp
  // Disabled by macro
  SPDLOG_LOGGER_TRACE(logger, message);
  // Compiled in, but runtime disabled by default
  logger->trace(message);
```
