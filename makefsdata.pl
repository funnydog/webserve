#!/usr/bin/perl

use 5.016;
use warnings;
use autodie;

open my $out, "> fsdata.c";

chdir("fs");

sub printhexstr {
    my ($out, $comment, $str, $zero) = @_;

    print $out "\n\t/* $comment */" if $comment;

    for (my $i = 0; $i < length($str); $i++) {
	if ($i % 10 == 0) {
	    print $out "\n\t";
	} else {
	    print $out " ";
	}
	printf $out "0x%02x,", unpack("C", substr($str, $i, 1));
    }

    print $out "0x00," if $zero // 0;
    print $out "\n";
}

sub printhexfile {
    my ($out, $comment, $path) = @_;

    open my $fh, "<", $path or die "Cannot read '$path': $!";

    print $out "\n\t/* $comment */" if $comment;

    my $i = 0;
    my $data;
    while(read($fh, $data, 1)) {
	if ($i % 10 == 0) {
	    print $out "\n\t";
	} else {
	    print $out " ";
	}
        printf $out "0x%02x,", unpack("C", $data);
	$i++;
    }
    print $out "\n";

    close $fh;
}

sub get_content_type {
    my $name = shift;

    if ($name =~ /\.plain$/ || $name =~ /cgi/) {
	return "";
    } elsif($name =~ /\.html$/) {
	return "Content-type: text/html\r\n";
    } elsif ($name =~ /\.gif$/) {
	return "Content-type: image/gif\r\n";
    } elsif ($name =~ /\.png$/) {
	return "Content-type: image/png\r\n";
    } elsif ($name =~ /\.jpg$/) {
	return "Content-type: image/jpeg\r\n";
    } elsif ($name =~ /\.class$/) {
	return "Content-type: application/octet-stream\r\n";
    } elsif ($name =~ /\.ram$/) {
	return "Content-type: audio/x-pn-realaudio\r\n";
    } else {
	return "Content-type: text/plain\r\n";
    }
}

my @fvars;
my @fnames;

print($out "#include <stdint.h>\n\n");
print($out "#include \"fsdata.h\"\n\n");

open my $filelist, "find . -type f |";
while(my $file = <$filelist>) {
    chomp $file;

    my $header = "";
    $header .= "Server: lwIP/pre-0.6 (http://www.sics.se/~adam/lwip/)\r\n";
    $header .= get_content_type($file);
    $header .= "\r\n";

    my $abspath = $file;
    $abspath =~ s/\.//;
    my $fvar = $abspath;
    $fvar =~ s-/-_-g;
    $fvar =~ s-\.-_-g;

    print $out "static const uint8_t data".$fvar."[] = {";

    printhexstr($out, $abspath, $abspath, 1);
    printhexstr($out, "header", $header);
    printhexfile($out, "contents", $file);

    print $out "\n};\n\n";

    push(@fvars, $fvar);
    push(@fnames, $abspath);
}
close $filelist;

print($out "struct fsdata rootfs[] = {\n");
for(my $i = 0; $i < @fvars; $i++) {
    my $file = $fnames[$i];
    my $fvar = $fvars[$i];

    print $out "\t{\n" .
	  "\t\t.path = (const char *)data$fvar,\n" .
	  "\t\t.data = data$fvar + " . (length($file) + 1) . ",\n" .
	  "\t\t.size = sizeof(data$fvar) - " . (length($file) + 1) . ",\n" .
	  "\t},\n";
}
print $out "\t{0},\n};\n";
