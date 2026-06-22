# Extract I/O port writes/reads from ZEsarUX cpu-transaction-log
# Usage: awk -f tools/extract_iotrace.awk /tmp/zsx_cold.log > /tmp/zsx_io.txt
$2 == "OUT" {
    port = $3
    gsub(/[(),A-Z]/, "", port)
    print "OUT " port " @ " $1
    next
}
$2 == "IN" && $3 ~ /^A,/ {
    port = $3
    gsub(/^A,/, "", port)
    gsub(/[(),]/, "", port)
    print "IN  " port " @ " $1
}
