# $Id: count.awk,v 1.1 2003/11/20 22:13:15 toshok Exp $
#
# Print out the number of log records for transactions that we
# encountered.

/^\[/{
	if ($5 != 0)
		print $5
}
