#!/usr/bin/env perl
# SPDX-FileCopyrightText: 2026 fuddlesworth
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Phase 4 sub-commit 7 housekeeping: migrate every
# `Behavior on X { (Number|Color)Animation { duration: ... } }`
# call-site in the in-tree QML to
# `Behavior on X { PhosphorMotionAnimation { profile: "<path>" } }`.
#
# Only Behavior-wrapped NumberAnimation / ColorAnimation with an
# explicit duration: field are migrated — programmatic
# SequentialAnimation / ParallelAnimation children with target /
# property / from / to are left alone (those carry specific explicit
# semantics the profile-based shape doesn't cover).
#
# Also adds `import org.phosphor.animation` to files touched by the
# migration if the import is missing. Idempotent — running twice is
# a no-op on already-migrated files.

use strict;
use warnings;

my $profile = $ARGV[0] // 'global';
shift @ARGV;
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

for my $file (@files) {
    my $text = read_file($file);
    my $orig = $text;

    # 1) Replace Behavior-wrapped (Number|Color)Animation { ... duration: ... ... }
    #    Only matches blocks that are immediate children of `Behavior on ... {`.
    #    The inner `[^}]*` rejects nested braces — so a NumberAnimation with a
    #    child block (target objects etc.) won't match, which is the desired
    #    gate for programmatic vs Behavior-triggered animations.
    #
    #    Non-brace regex delimiters (`!`) so the literal braces in the QML
    #    pattern don't need escaping against Perl's brace-delimited-regex
    #    counting heuristic.
    $text =~ s!
        (                                   # Capture group 1: Behavior-on prefix + opening brace
            \bBehavior \s+ on \s+ \S+ \s*   # Behavior on <property>
            \x7b                            # Behavior opening brace
            \s*
        )
        (?:Number|Color)Animation           # Animation subclass
        \s* \x7b                             # Animation opening brace
        [^\x7d]*                             # Animation contents (no nested braces)
        \x7d                                 # Animation closing brace
    !
        $1 . qq(PhosphorMotionAnimation \x7b\n            profile: "$profile"\n        \x7d)
    !gsxe;

    # 2) Add `import org.phosphor.animation` if the file was modified and
    #    the import is missing. Inserted after the last existing `import`
    #    line so it lands alongside the rest.
    if ($text ne $orig && $text !~ m!^\s*import \s+ org\.phosphor\.animation!xm) {
        $text =~ s!
            ( (?: ^ \s* import \s+ [^\n]+ \n )+ )
        !
            $1 . qq(import org.phosphor.animation\n)
        !xme;
    }

    if ($text ne $orig) {
        write_file($file, $text);
        print "migrated: $file\n";
    } else {
        print "unchanged: $file\n";
    }
}
