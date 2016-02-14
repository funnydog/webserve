#!/usr/bin/perl

use 5.016;
use warnings;
use autodie;
use strict;

use Getopt::Std;

sub printhexfile {
    my ($out, $path, $varname) = @_;

    open my $fh, "<", $path or die "Cannot read '$path': $!\n";

    print $out "const uint8_t ${varname}[] = {";

    my $i = 0;
    my $data;
    while(read($fh, $data, 1)) {
	if ($i % 10 == 0) {
	    print $out "\n\t";
	} else {
	    print $out " ";
	}

        printf $out "%#.02x,", unpack("C", $data);
	$i++;
    }
    close $fh;

    print $out "\n0};\n";
    print $out "const size_t ${varname}_len = sizeof(${varname});\n\n";
}

my %options = ();
getopts("c:s:k:", \%options);

open my $out, "> certs.c";
print $out "#include <stdint.h>\n";
print $out "#include <stdlib.h>\n\n";
printhexfile($out, $options{c}, "ca_cert") if defined $options{c};
printhexfile($out, $options{s}, "server_cert") if defined $options{s};
printhexfile($out, $options{k}, "server_key") if defined $options{k};
print $out, "\n";

close $out;
