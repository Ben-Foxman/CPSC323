#!/usr/bin/python
# Ben Foxman, netid btf28
# Problem Set 3 - McBash, a bash-esque macroprocessor written in python.
import os
import re
import sys

NAME = "\$[A-Za-z_]\w*"  # $NAME
BRACENAME = "\${[A-Za-z_]\w*}"  # ${NAME}
NUMBER = "\$\d"  # $N
BRACENUMBER = "\${\d+}" # ${N}
ALL = "\$\*"  # $*
MINUS = "\${[A-Za-z_]\w*-"  # {NAME-
EQUALS = "\${[A-Za-z_]\w*="  # {NAME=


def main():
    line = 1 # line count

    while True:
        try:
            print("({})$ ".format(line), flush=True, end="")
            command = sys.stdin.readline()
            if not command:
                print()
                break
            # command = input("({})$ ".format(line))
        except:
            print()
            break

        if re.match("\s*$", command):  # empty input
            continue

        expansion = ""  # new string to build
        success = True  # expansion is successful

        while command:  # input non-empty
            new, command, success = expand(command, False)
            expansion += new
            if not success:
                print("Invalid expansion", file=sys.stderr)
                break

        if success:
            print(">> {}".format(expansion), flush=True, end="")
            line += 1


def expand(command, suppressErrors):
    types = [NAME, NUMBER, ALL, BRACENAME, BRACENUMBER, MINUS, EQUALS]  # all 7 types of expansions
    match = None  # hold information about the match, None is no match
    expansion = ""  # the new expansion

    i = 0
    while not match and i < len(types):  # no matches found
        match = re.match(types[i], command)
        if match:
            break
        i += 1

    if match and not suppressErrors:  # cases when an initial match found - make the expansion
        if types[i] == NAME:   # expand $NAME
            if command[1: match.end()] in os.environ:
                expansion = os.environ[command[1: match.end()]]
            command = command[match.end():]

        elif types[i] == NUMBER:  # expand $digit
            index = int(command[1])
            if index < len(sys.argv):
                expansion = sys.argv[index]
            command = command[match.end():]

        elif types[i] == ALL:  # expand $*
            expansion = " ".join(sys.argv[1:])
            command = command[match.end():]

        elif types[i] == BRACENAME:  # expand ${NAME}
            if command[2: match.end() - 1] in os.environ:
                expansion = os.environ[command[2: match.end() - 1]]
            command = command[match.end():]

        elif types[i] == BRACENUMBER:  # expand ${N}
            index = int(command[2: match.end() - 1])
            if index < len(sys.argv):
                expansion = sys.argv[index]
            command = command[match.end():]

        else:  # expand ${NAME=WORD} or ${NAME-WORD}
            name = command[2: match.end() - 1]  # isolate NAME
            command = command[match.end():]  # strip NAME from command
            inWord = True

            if name in os.environ:  # if NAME exists use its expansion + suppress errors
                expansion = os.environ[name]
                suppressErrors = True

            while inWord:
                if command == "":  # ERROR 3, WORD not followed by }
                    return 'ERROR', 'ERROR', False
                elif command[0] == '}':  # WORD over
                    inWord = False
                else:
                    new, command, success = expand(command, suppressErrors)
                    if not suppressErrors:
                        expansion += new

            command = command[1:]  # SUCCESS: chop off }

            if types[i] == EQUALS:  # = ->  NAME receives that expansion
                os.environ[name] = expansion

        remaining = command

    elif command[:2] == '${' and suppressErrors:  # special case: skip over "junk" in ${NAME=WORD} if name exists
        inWord = True
        command = command[2:]
        while inWord:
            if command == "":  # ERROR 3, WORD not followed by }
                return 'ERROR', 'ERROR', False
            elif command[0] == '}':  # WORD over
                inWord = False
            else:
                new, command, success = expand(command, suppressErrors)
        remaining = command[1:]

    else:  # cases when no match found
        if command[:2] == '${' and not suppressErrors:  # ERROR 1, 2, 4
            return 'ERROR', 'ERROR', False

        elif command[0] == '\\':  # escape char - remove additional char
            expansion += command[:2]
            remaining = command[2:]
        else:  # normal char
            expansion += command[:1]
            remaining = command[1:]

    return expansion, remaining, True


if __name__ == "__main__":
    main()

