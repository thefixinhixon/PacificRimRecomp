#!/usr/bin/env python3
"""Freeze the build-graph codegen rule in generated/rexglue.cmake.

Codegen regenerates rexglue.cmake on every run, restoring the live
rule - which then re-fires during builds and OVERWRITES the hand
fixes (fix-tailcalls). This replaces the rule with a
stamp-only no-op: codegen for this project is a manual CLI step.
Idempotent: exits quietly if the freeze is already in place.
"""
import os
import sys

BLOCK = '''# HOUSE (Pacific Rim): codegen is a MANUAL step for this project (rexglue CLI),
# same end-state as The Maw / Bejeweled ports. The build graph must never
# re-run it: the emitted sources carry hand fixes (scripts/fix-tailcalls.py)
# that a re-run would erase, and depfile path drift re-fires it spuriously.
# This rule only stamps. Re-apply via scripts/refix.sh after every codegen.
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/generated/default/codegen.build.stamp"
           ${REXGLUE_ENTRYPOINT_GENERATED_SOURCES}
    COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_CURRENT_SOURCE_DIR}/generated/default/codegen.build.stamp"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Codegen is manual for pacificrim (see scripts/); stamp only"
    VERBATIM
)
add_custom_target(pacificrim_codegen
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/generated/default/codegen.build.stamp")'''

START = '# Codegen runs as part of the build'
END = ('add_custom_target(pacificrim_codegen\n'
       '    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/generated/default/codegen.build.stamp")')


def main():
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        '..', 'generated', 'rexglue.cmake')
    src = open(path).read()
    if 'stamp only' in src:
        print('freeze already in place')
        return 0
    start = src.index(START)
    end = src.index(END) + len(END)
    open(path, 'w').write(src[:start] + BLOCK + src[end:])
    print('codegen rule frozen (stamp only)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
