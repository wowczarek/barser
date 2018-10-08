# Barser - the BAstard paRSER

## About

Barser is a general-purpose flexible hierarchical configurational parser / dictionary with a tree structure.

Although Barser will parse JSON files, it is not a JSON parser. An alternative parse function could be written to deal with JSON specifically - and it would be much faster because it would not have to deal with special cases, namely multiple consecutive tokens. Barser will parse JSON and many other variants of curly bracket syntax, but the native format it can work with is similar to Juniper Networks' JunOS configuration files.

Barser is work in progress. At this time (8 Oct 2018) Barser passes feedback tests (generating an output file (2) from an original source input file (1), and then parsing its own output to produce another output (3) - (2) and (3) are identical.

### Features:

- Parsing files and dumping the ouput
- User-friendly parser error output (line number / position, contents of the affected line
- Very loose and flexible input format
- Support for C-style and Unix-style comments
- Support for C-style multiline comments
- Support for basic escape characters in quoted strings
- Character classes and meanings all defined in a separate header file, `barser_defaults.h`

### Plans:

- Implement indexing of inserted tree nodes using a red-black tree index
- Implement a simple query language (direct queries in the form of "/node/child/grandchild" will be supported as soon as indexing is implemented
- Implement variable support / string replacement and automatic content generation
- Implement merge and diff operations
- Implement stage 2 parsing of stored string values to other data types
- Investigate wide character support
- Implement alternative output formats (JSON, XML - in theory)

### Supported data format

For the lack of a proper reference / 


Barser was developed around the concepts used in JunOS config files (but Extreme Networks and XORP use similar formats), because they felt like a useful thing to have. Concepts such as "instances". An collection of instances looks like this:

```
cars {

	car bob {
	    doors 3;
	}

	car steve {
	    doors 5;
	}

	car jake {
	    doors 8;
	}
}
```
In terms of the resulting tree hierarchy, there is only one `car` branch, and internally the tree really looks like this:

```
cars {

	car {
		bob {
			doors 3;
		}

		steve {
			doors 5;
		}

		jake {
			doors 8;
		}

	}

}
```

Barser supports more similar concepts based on consecutive tokens, and other bits and pieces to accept a format that is easy / fast to type by hand. For example array members need no termination character (`;` - but `,` is also accepted, as per JSON, and so is `|`). So an array can be constructed like this:

```
cars [ bob steve jake ];
```
Or like this:
```
cars [ bob, steve, jake];
```
Or like this:
```
cars [ bob | steve | jake ];
```
Or like this:
```
cars [ bob; steve; jake ];
```
This is because `|` is treated as whitespace, and value termination `;` and `,` is  optional inside an array.


Array members are anonymous - you can nest a branch in an array:
```
cars [ bob {something: true; somethingelse false;} jake ]
```
This array has three members, the middle one is a branch with two leaves.

This however (last branch):

```
pushbike {

    wheels 2;

    gears {

	front 4;
	rear 6;
    }

/* nope */

    { spokes 128; hubs 2; }

}

```
Is illegal and will produce an error. Etc. Same goes for arrays - they can be anonymous when nested, but otherwise not.

Value termination is optional for the last element in a block.

Character class mappings (`barser_defaults.h`) assign flags rather than unique classes to each ASCII character. This means that a character can be treated as whitespace in one context, but as part of a token in another. This is the case with `:` for example. `:` is illegal in the first token in a sequence, but is legal in subsequent tokens. This allows both skipping over JSON's `:` and including `:` in unquoted literals like BGP communities - not that this was written to deal with networks.

Yes, this is all very loose, but *flexible* is the key.

### Operation

Barser scans the input buffer byte by byte, skipping whitespaces, waiting for control characters and recognising character classes based on a 256-slot lookup table. As the scanner state machine passes through different stages, events are raised and processed accordingly. Barser accumulates string tokens in a stack and processes them once a specific control element or token count is reached - the scanner raises an event which is then picked up by the worker function inserting nodes.

