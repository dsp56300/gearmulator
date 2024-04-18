#!/bin/sh

fst_h="$1"

if [ "x${fst_h}" = "x" ]; then
 fst_h=fst/fst.h
fi

extract_values() {
 egrep "FST_(HOST|EFFECT|TYPE|CONST|FLAG|SPEAKER)"  "$1" \
	 | egrep -v "# *define " \
	 | grep -v "FST_fst_h"
}

version=$( \
extract_values "${fst_h}" \
| grep -v UNKNOWN \
| grep -c . \
)
unknown=$( \
extract_values "${fst_h}" \
| egrep "FST_.*_UNKNOWN" \
| grep -c . \
)


sed -e "s|\(# *define  *FST_MINOR_VERSION  *\)[0-9]*$|\1${version}|" -i "${fst_h}"
echo "version: ${version}"
echo "identifiers: known:${version} unknown:${unknown}"
