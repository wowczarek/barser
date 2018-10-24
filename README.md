# Barser - the BAstard paRSER

![A drawing depicting a man breaking curly brackets with a spiked club, spraying blood all around](https://github.com/wowczarek/barser/raw/master/img/barser.png "This is how a Bastard Parser works")

## About

Barser is a general-purpose flexible hierarchical configurational parser / dictionary with a tree structure. Barser is built for features and convenience first, and for speed second.

The "Bastard" in the name refers to the supported format, not the parser itself, as in: it will parse any old bastard of a config file.

- Although Barser will parse JSON files, it is not a JSON parser. An alternative parse function could be written to deal with JSON specifically - and it would be much faster because it would not have to deal with special cases, namely multiple consecutive tokens. Barser will parse JSON and many other variants of curly bracket syntax, but the native format it can work with is similar to Juniper Networks' JunOS configuration files (or actually gated if you still remember it).

- Barser is work in progress. Barser currently passes feedback tests (generating an output file (2) from an original source input file (1), and then parsing its own output to produce another output (3) : (2) and (3) are identical. Barser can now also consume and reproduce a large and complex JunOS configuration (50k+ stanzas) which is _almost_ readily importable - JunOS native config parser is unfortunately content-sensitive, so a generic approach will not work - or it will, but output will be uglier than the original. A designated JunOS output mode may be introduced later - JunOS was never the target, only an inspiration.

- Barser is "somewhat" heavy on memory use - it is not designed for memory-constrained or embedded systems.

## Features

- Parsing files and dumping the ouput
- User-friendly parser error output (line number / position, contents of the affected line)
- Very loose and flexible input format
- Support for C-style and Unix-style comments
- Support for C-style multiline comments
- Support for basic escape characters in quoted strings
- Character classes and meanings all defined in a separate header file, `barser_defaults.h`
- Basic operations on the resulting structure - searches, retrieving nodes, duplication, deletion, renaming
- Indexed operation (red-black tree based) or indexless

## Todo / progress

- Implement a good node hashing strategy **[done]**. Using xxhash of node name, XOR-mixed with parent's hash. Mixing works reasonably well - total of 22k collisions for citylots.js at 13M nodes, max nodes per hash 2.
- Implement indexing of inserted tree nodes using a red-black tree index initially **[slow, but done]**
- Implement dynamic linked lists to deal with collisions (this is beyond the index and any collision resolving strategy - fast, non-crypto hashes WILL collide) **[done]**
- Implement direct queries / node retrieval in the form of "/node/child/grandchild" **[done]** (trailing and leading "`/`"'s are removed)
- Implement node renaming (and thus recursive rehashing) **[done]**
- Implement dictionary copying / duplication **[done]**
- Implement callback walks / iteration **[done]**
- Implement merge and diff operations
- Implement support for multiline quoted strings
- Implement stage 2 parsing of stored string values to other data types
- Implement a simple query language - target is to support at least `"*"` for _any string_ and `"?"` for _any character_
- Implement variable support / string replacement (`@variables { bob "square";} shapes { box "@bob@"; }`) and automatic content generation ( `@generate "seq var 1 1000" "test@var@" { hello 5; this "number@var@";}`)
- Investigate wide character support **[meh]**
- Implement alternative output formats (JSON output, XML output - maybe)
- Implement alternative parser loops for specific content types

## Supported data format

For the lack of a proper reference / standard, here is some loose information about the input format.

The general format is a tree structure, and sub-trees are surrounded with `{}`. Nodes with no children and an optional value are leaves. Arrays are declared by surrounding a set of values with `[]`. Leaves must be terminated with `;`, unless they are the last node in a block. Quoted strings can contain escape characters. They are unescaped when parsed, and escaped again when dumped.

This example contains all of the standard elements:

```
/* the whole content can optionally be
 * surrounded with '{}' - this does not create an extra node */
{

    dogs {

        poodle { // I'm a Guadepoodle, yes I'm sure, my new boots are all cat fur
            age : 5;
            sex = "M";
            favourite_quotation "To\nbe\nor\n\t\tnot\nto\nbe";
            location "Guadeloupe";
            targets [ trees, postman | drunks : Jacques Marie ]

            cats-maimed {
                cat "D'Artagnan" { date_maimed "yesterday"; fight-duration_ns "4"; }
                cat "Cousteau" { date_maimed "today"; fight-duration_ns "3.52e9" }
            }
        }
    }

}
```
When parsed back, this turns into:

```
dogs {
    poodle {
        age 5;
        sex "M";
        favourite_quotation "To\nbe\nor\n\t\tnot\nto\nbe";
        location "Guadeloupe";
        targets [ trees postman drunks Jacques Marie ];
        cats-maimed {
            cat "D'Artagnan" {
                date_maimed "yesterday";
                fight-duration_ns "4";
            }
            cat "Cousteau" {
                date_maimed "today";
                fight-duration_ns "3.52e9";
            }
        }
    }
}
```

Barser was developed around the concepts used in gated / JunOS config files (but Extreme Networks and XORP use similar formats), because they felt like a useful thing to have. Concepts such as "instances". An collection of instances looks like this:

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


Array members are anonymous. You can nest a branch in an array:
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

## Operation

Barser scans the input buffer byte by byte, skipping whitespaces, waiting for control characters and recognising character classes based on a 256-slot lookup table. As the scanner state machine passes through different stages, events are raised and processed accordingly. Barser accumulates string tokens in a stack and processes them once a specific control element or token count is reached - the scanner raises an event which is then picked up by the worker function inserting nodes.

Barser does not reuse the existing buffer. The buffer could come from an mmaped file for example - and what happens then? Also the dictionary is to be mutable. For those reasons strings are dynamically allocated and live in the dictionary. Unquoted tokens are copied from the buffer, but quoted strings grow as they are copied byte by byte, because they need to be checked for escape sequences.

## Testing

Provided is a test program / benchmark, `barser_test.c`. Options:
```
$ ./barser_test -h

barser_test (c) 2018: Wojciech Owczarek, a flexible hierarchical configuration parser

usage: barser_test <-f filename> [-q query] [-Q] [-N NUMBER] [-p] [-d] [-X]

-f filename     Filename to read data from (use "-" to read from stdin)
-q query        Retrieve nodes based on query and dump to stdout
-Q              Test random node fetch
-N NUMBER       Number of nodes to fetch (-Q), default: min(20000, nodecount)
-p              Dump parsed data to stdout
-d              Test dictionary duplication
-X              Build an unindexed dictionary
```

**Example output for a ~180 MB's worth of JunOS config:**

* Indexed:

```
$ ./barser_test -f ../barserdata/junos20 -Q -N 100000
Loading "../barserdata/junos20" into memory... done.
Loaded 178233894 bytes in 47766904 ns, 3731.326 MB/s
Parsing data... done.
Parsed in 8121144069 ns (indexed), 21.947 MB/s, 5265301 nodes, 648345 nodes/s
Extracting random 100000 nodes... done.
Getting 100000 random paths from dictionary... done.
Found 100000 out of 100000 nodes (indexed), average 3113 ns per fetch
Freeing test data... done.
Freeing dictionary... done.
Freed in 1417070776 ns, 5265301 nodes, 3715623 nodes/s

```

* Unindexed (this structure is very shallow, with few children at each node):

```
$ ./barser_test -f ../barserdata/junos20 -Q -N 100000 -X
Loading "../barserdata/junos20" into memory... done.
Loaded 178233894 bytes in 45143494 ns, 3948.163 MB/s
Parsing data... done.
Parsed in 1193688712 ns (unindexed), 149.314 MB/s, 5265301 nodes, 4410950 nodes/s
Extracting random 100000 nodes... done.
Getting 100000 random paths from dictionary... done.
Found 100000 out of 100000 nodes (unindexed), average 6153 ns per fetch
Freeing test data... done.
Freeing dictionary... done.
Freed in 186368836 ns, 5265301 nodes, 28252046 nodes/s
```

**Example output for [citylots.json](https://github.com/zemirco/sf-city-lots-json):**

(This is a big and nasty JSON file with huge arrays)

* Indexed - ingestion is piss-slow, but search is decent:

```
$ ./barser_test -f ../barserdata/citylots.json -Q -N 100000
Loading "../barserdata/citylots.json" into memory... done.
Loaded 189778221 bytes in 48177730 ns, 3939.127 MB/s
Parsing data... done.
Parsed in 23612471449 ns (indexed), 8.037 MB/s, 13805884 nodes, 584686 nodes/s
Extracting random 100000 nodes... done.
Getting 100000 random paths from dictionary... done.
Found 100000 out of 100000 nodes (indexed), average 2724 ns per fetch
Freeing test data... done.
Freeing dictionary... done.
Freed in 4582267068 ns, 13805884 nodes, 3012894 nodes/s
```

* Unindexed - ingestion is reasonably fast, but search times are truly atrocious (big arrays need naive linked list traversal):

```
$ ./barser_test -f ../barserdata/citylots.json -Q -N 100000 -X
Loading "../barserdata/citylots.json" into memory... done.
Loaded 189778221 bytes in 45704952 ns, 4152.246 MB/s
Parsing data... done.
Parsed in 2543231229 ns (unindexed), 74.621 MB/s, 13805884 nodes, 5428482 nodes/s
Extracting random 100000 nodes... done.
Getting 100000 random paths from dictionary... done.
Found 100000 out of 100000 nodes (unindexed), average 2277371 ns per fetch
Freeing test data... done.
Freeing dictionary... done.
Freed in 520960318 ns, 13805884 nodes, 26500836 nodes/s
```

Yes, two milliseconds. 1e6 times worse than with the red-black tree index. Naive search times were roughly halved after linked list traversal was implemented to search from both ends of the list - so it was even worse at the start.
