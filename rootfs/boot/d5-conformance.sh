#!/bin/sh
# Original shared regressions for the frozen Silt shell profile.
D5_SHELL=${D5_SHELL:-/bin/sh}
export D5_SHELL
passed=0
check() {
    if [ "$2" != "$3" ]; then
        printf 'D5_CASE_FAIL: %s got=<%s> expected=<%s>\n' "$1" "$2" "$3"
        exit 1
    fi
    passed=$((passed + 1))
    printf 'D5_CASE_PASS: %s\n' "$1"
}
check arithmetic "$(( (7 + 5) * 3 / 2 ))" 18
check defaults "${unset_d5:-fallback}" fallback
value=abcdef
check trim "${value#a*}${value%f}" bcdefabcde
check command-sub "$(printf 'one\ntwo\n\n')" 'one
two'
check field-split "$(v='a b'; set -- $v; printf '%s:%s:%s' "$#" "$1" "$2")" 2:a:b
check quoted-empty "$(set -- '' 'two words'; printf '%s:<%s>:<%s>' "$#" "$1" "$2")" '2:<>:<two words>'
check glob-class "$(case b in [a-c]) echo yes;; esac)" yes
# Upstream 8fcf8a7: a dollar-single-quoted backslash stays literal in patterns.
check dollarsq-pattern "$(case 'a\b' in *$'\\'*) echo literal;; esac)" literal
check octal-quote "$(printf '%s' $'\141\142')" ab
check function-return "$(f() { return 17; }; f; echo "$?")" 17
check subshell-isolation "$(v=outer; (v=inner); echo "$v")" outer
check short-circuit "$(false && echo bad; true || echo bad; echo yes)" yes
check loop "$(for v in a b c; do printf '%s' "$v"; done)" abc
check case-fallthrough "$(case x in y) echo no;; x) echo yes;; esac)" yes
check read-ifs "$(printf 'a:b\n' | (IFS=: read -r a b; printf '%s,%s' "$a" "$b"))" a,b
check heredoc "$(v=expanded; while IFS= read -r line; do printf '%s' "$line"; done <<END
$v
END
)" expanded
check quoted-heredoc "$(v=expanded; while IFS= read -r line; do printf '%s' "$line"; done <<'END'
$v
END
)" '$v'
check external-empty "$("$D5_SHELL" -c 'printf "%s:<%s>" "$#" "$1"' x '')" '1:<>'
check export-exec "$(D5_EXPORT=kept; export D5_EXPORT; "$D5_SHELL" -c 'echo "$D5_EXPORT"')" kept
check exit-trap "$("$D5_SHELL" -c 'trap '\''echo status=$?'\'' EXIT; exit 19')" status=19
printf 'D5_CONFORMANCE: %s passed\n' "$passed"
