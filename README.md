# Barser - the BAstard paRSER

## About

Barser is a general-purpose flexible hierarchical configurational parser / dictionary with a tree structure.

It will parse JSON and many other variants of curly bracket syntax, but the native format it can work with is similar to Juniper Networks' JunOS configuration files. In fact Barser was developed specifically to include concepts used in those files.

Work in progress. At this time (8 Oct 2018) Barser passes feedback tests (generating an output file (2) from an original source input file (1), and then parsing its own output to produce another output (3) - (2) and (3) are identical.

###Features:

- Parsing files and dumping the ouput
- User-friendly parser error output (line number / position, contents of the affected line
- Very loose and flexible input format
- Support for C-style and Unix-style comments
- Support for C-style multiline comments
- Support for basic escape characters in quoted strings
- Character classes and meanings all defined in a separate header file, `barser_defaults.h`

###Plans:

- Implement indexing of inserted tree nodes using a red-black tree index
- Implement a simple query language (direct queries in the form of "/node/child/grandchild" will be supported as soon as indexing is implemented
- Implement variable support / string replacement and automatic content generation
- Implement merge and diff operations
- Implement stage 2 parsing of stored string values to other data types
- Investigate wide character support

### Supported data formats

### Operation

Barser scans the input buffer byte by byte, skipping whitespaces, waiting for control characters and recognising character classes based on a 256-slot lookup table. As the scanner state machine passes through different stages, events are raised and processed accordingly. Barser accumulates string tokens (up to a limit) and processes them once a specific control element or token count is reached.
