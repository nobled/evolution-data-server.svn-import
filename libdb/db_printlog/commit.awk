# $Id: commit.awk,v 1.1 2003/11/20 22:13:15 toshok Exp $
#
# Output tid of committed transactions.

/txn_regop/ {
	print $5
}
