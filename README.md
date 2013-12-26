# pipeopts

Getopts for process pipelines

## Synopsis

     # list recently modified files 
     lr() {
	   pipeopts "adl|n:c:" "$*"
       ls -t ${PIPEOPTS[0]} | head ${PIPEOPTS[1]}
     }

     # now run it
     lr -aln25

## Description

`pipeopts` is a Bash builtin that helps you pass arguments to process pipelines.

<!-- For those not wanting to dick around with building a Bash builtin (or for those not using Bash)
 there is `pipeopts.pl` -->

## Etc...

Internal Bash functions, useful when writing loadable builtins: https://gist.github.com/sshaw/8017032




