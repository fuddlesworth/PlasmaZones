#!/usr/bin/env perl
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Migrate every Behavior-wrapped `(Number|Color)Animation { ... }`
# call-site in the in-tree QML to
# `PhosphorMotionAnimation { profile: "<path>" }`, and insert
# `import org.phosphor.animation` where missing.
#
# Properly handles QML lexical structure:
#   - matching braces (including nested `easing { ... }` blocks)
#   - double- and single-quoted string literals (never rewrite inside)
#   - line comments `// ...` and block comments `/* ... */`
#
# Idempotent: re-running against already-migrated files produces no
# diff (the NumberAnimation/ColorAnimation pattern is gone; the
# import-dedup guard tolerates `import org.phosphor.animation as X`
# aliases so a file that aliased on a prior run isn't double-imported).
#
# Usage:
#   scripts/migrate-qml-animations.pl --profile <path> FILE [FILE...]
#
# `--profile` is REQUIRED. Early versions defaulted to "global" when
# the argument was missing, silently producing the single-shared-
# profile pathology that the PR-344 review flagged; now the script
# refuses to guess.

use strict;
use warnings;
use Getopt::Long qw(GetOptions);

my $profile;
my $dry_run;
GetOptions(
    'profile=s' => \$profile,
    'dry-run'   => \$dry_run,
) or die "usage: $0 --profile <path> [--dry-run] FILE [FILE...]\n";

die "$0: --profile <path> is required (pass the registry path string used in QML)\n"
    unless defined $profile && length $profile;
die "$0: --profile must not contain a double-quote\n"
    if $profile =~ /"/;
die "$0: no input files\n" unless @ARGV;

my @files = @ARGV;

sub read_file {
    my ($path) = @_;
    open my $fh, '<:encoding(UTF-8)', $path or die "Cannot read $path: $!";
    local $/;
    my $content = <$fh>;
    close $fh;
    return $content;
}

sub write_file {
    my ($path, $content) = @_;
    open my $fh, '>:encoding(UTF-8)', $path or die "Cannot write $path: $!";
    print $fh $content;
    close $fh;
}

# Scan forward from $start (which points at the opening `{`) and
# return the index of the matching closing `}`, respecting QML lexical
# nesting. Returns -1 if unmatched.
sub find_matching_brace {
    my ($text, $start) = @_;
    my $len   = length $text;
    my $depth = 0;
    my $i     = $start;
    while ($i < $len) {
        my $c = substr($text, $i, 1);
        if ($c eq '"' || $c eq "'") {
            # Skip string literal, honouring backslash escapes.
            my $quote = $c;
            $i++;
            while ($i < $len) {
                my $d = substr($text, $i, 1);
                if ($d eq '\\') { $i += 2; next; }
                if ($d eq $quote) { $i++; last; }
                if ($d eq "\n") { last; }  # unterminated — don't hang
                $i++;
            }
            next;
        }
        if ($c eq '/' && $i + 1 < $len) {
            my $next = substr($text, $i + 1, 1);
            if ($next eq '/') {
                # Line comment — skip to EOL.
                my $nl = index($text, "\n", $i);
                $i = $nl == -1 ? $len : $nl + 1;
                next;
            }
            if ($next eq '*') {
                # Block comment — skip to `*/`.
                my $end = index($text, '*/', $i + 2);
                $i = $end == -1 ? $len : $end + 2;
                next;
            }
        }
        if ($c eq '{') {
            $depth++;
            $i++;
            next;
        }
        if ($c eq '}') {
            $depth--;
            return $i if $depth == 0;
            $i++;
            next;
        }
        $i++;
    }
    return -1;
}

# Find the indentation (leading-whitespace prefix) of the line
# containing $pos. Used so the replacement reads naturally with the
# source's existing indent style.
sub indent_of_line_at {
    my ($text, $pos) = @_;
    my $line_start = rindex($text, "\n", $pos - 1) + 1;
    my $i          = $line_start;
    while ($i < $pos && substr($text, $i, 1) =~ /\s/ && substr($text, $i, 1) ne "\n") {
        $i++;
    }
    return substr($text, $line_start, $i - $line_start);
}

# In-code scan of $text: find every `(Number|Color)Animation {` that
# is DIRECTLY the first statement inside a `Behavior on X {` block,
# replace its entire `{ ... }` body with a PhosphorMotionAnimation
# block. Returns the rewritten text.
sub migrate_one_file {
    my ($text) = @_;
    my $out    = '';
    my $i      = 0;
    my $len    = length $text;

    # Walk the text; emit characters verbatim except when we detect
    # the `Behavior on X {` prefix with a direct child animation.
    while ($i < $len) {
        my $c = substr($text, $i, 1);

        # Pass through string literals unchanged.
        if ($c eq '"' || $c eq "'") {
            my $quote = $c;
            my $start = $i;
            $i++;
            while ($i < $len) {
                my $d = substr($text, $i, 1);
                if ($d eq '\\') { $i += 2; next; }
                if ($d eq $quote) { $i++; last; }
                if ($d eq "\n")   { last; }
                $i++;
            }
            $out .= substr($text, $start, $i - $start);
            next;
        }

        # Pass through comments unchanged.
        if ($c eq '/' && $i + 1 < $len) {
            my $n = substr($text, $i + 1, 1);
            if ($n eq '/') {
                my $nl = index($text, "\n", $i);
                my $end = $nl == -1 ? $len : $nl + 1;
                $out .= substr($text, $i, $end - $i);
                $i = $end;
                next;
            }
            if ($n eq '*') {
                my $bc_end = index($text, '*/', $i + 2);
                my $end = $bc_end == -1 ? $len : $bc_end + 2;
                $out .= substr($text, $i, $end - $i);
                $i = $end;
                next;
            }
        }

        # Try to match `Behavior on <property> {` at the current
        # position. The property name can be a simple identifier or a
        # dotted qualifier (Behavior on Item.opacity {} is rare but
        # legal). Anchored via \G to avoid mid-token matches.
        pos($text) = $i;
        if ($text =~ /\G\bBehavior\s+on\s+[\w.]+\s*\{/gc) {
            my $match_start = $i;
            my $behavior_close = find_matching_brace($text, pos($text) - 1);
            if ($behavior_close == -1) {
                # Unmatched — give up and pass the keyword through.
                $out .= substr($text, $i, pos($text) - $i);
                $i = pos($text);
                next;
            }

            my $body_start = pos($text);
            my $body       = substr($text, $body_start, $behavior_close - $body_start);

            # Only rewrite if the Behavior's body starts (modulo
            # whitespace / comments) with (Number|Color)Animation {.
            # We peek via a non-consuming regex.
            if ($body =~ /\A\s*(?:Number|Color)Animation\s*\{/) {
                # Find the animation's opening brace relative to body.
                if ($body =~ /\A(\s*)(Number|Color)Animation(\s*)\{/) {
                    my $anim_brace_offset = length($1) + length($2) + length("Animation") + length($3);
                    my $anim_brace_abs    = $body_start + $anim_brace_offset;
                    my $anim_close_abs    = find_matching_brace($text, $anim_brace_abs);
                    if ($anim_close_abs != -1 && $anim_close_abs < $behavior_close) {
                        # Everything between Animation's `{` and `}`
                        # is the body we drop. Build replacement with
                        # indentation matching the Animation line.
                        my $anim_line_indent = indent_of_line_at($text, $body_start + length($1));
                        my $replacement =
                              "PhosphorMotionAnimation {\n"
                            . $anim_line_indent . "    "
                            . qq(profile: "$profile"\n)
                            . $anim_line_indent . "}";

                        # Emit: Behavior prefix + leading body whitespace
                        # + replacement + trailing body content
                        # (anything after the animation and before the
                        # Behavior's closing brace — typically empty).
                        $out .= substr($text, $i, $body_start - $i);     # "Behavior on X {"
                        $out .= $1;                                       # leading whitespace inside body
                        $out .= $replacement;
                        $out .= substr($text, $anim_close_abs + 1, $behavior_close - ($anim_close_abs + 1));
                        $out .= '}';                                      # Behavior closing brace
                        $i = $behavior_close + 1;
                        next;
                    }
                }
            }

            # Fell through — pass the matched "Behavior on X {" prefix
            # as-is and continue scanning from after the opening brace.
            $out .= substr($text, $i, pos($text) - $i);
            $i = pos($text);
            next;
        }

        # Default: emit the character and advance.
        $out .= $c;
        $i++;
    }

    return $out;
}

# Insert `import org.phosphor.animation` after the last existing
# import line if the module is not already imported (any alias form).
sub ensure_import {
    my ($text) = @_;
    # Already imported (with or without alias)?
    return $text if $text =~ m{^\s*import\s+org\.phosphor\.animation\b}xm;
    # Insert after the last existing top-of-file import block.
    if ($text =~ s{
        ( (?: ^ \s* import \s+ [^\n]+ \n )+ )
    }{$1 . "import org.phosphor.animation\n"}xme) {
        return $text;
    }
    # No existing imports at all — unusual for QML but handle
    # gracefully: prepend.
    return "import org.phosphor.animation\n" . $text;
}

my $touched = 0;
for my $file (@files) {
    my $orig = read_file($file);
    my $new  = migrate_one_file($orig);
    $new = ensure_import($new) if $new ne $orig;

    if ($new ne $orig) {
        if ($dry_run) {
            print "would migrate: $file\n";
        } else {
            write_file($file, $new);
            print "migrated: $file\n";
        }
        $touched++;
    } else {
        print "unchanged: $file\n";
    }
}

print "\n$touched file(s) " . ($dry_run ? "would be " : "") . "migrated.\n";
