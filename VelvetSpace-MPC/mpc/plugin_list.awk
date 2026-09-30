# Edit the plugin list inside MPC.settings (BusyBox-awk compatible).
#   awk -v mode=add    -v file=/sdcard/vst/x.so -v entryfile=plugin.xml -f plugin_list.awk MPC.settings
#   awk -v mode=remove -v file=/sdcard/vst/x.so -f plugin_list.awk MPC.settings
# Any existing <PLUGIN .../> element (one line or wrapped over several) whose file= matches is dropped first,
# so "add" is idempotent. In add mode the entry goes into <VALUE name="pluginList-arm"><KNOWNPLUGINS>,
# creating the value just before </PROPERTIES> if it does not exist yet.
function emit_pending(   hit) {
    hit = index(pending, "file=\"" file "\"")
    if (!hit) print pending
    pending = ""
}
BEGIN {
    added = 0; inlist = 0; pending = ""
    if (mode == "add") { while ((getline line < entryfile) > 0) entry = entry line; close(entryfile) }
}
# continuation of a wrapped <PLUGIN ...> element
pending != "" { pending = pending "\n" $0; if ($0 ~ /\/>/) emit_pending(); next }
/<PLUGIN( |$)/ { pending = $0; if ($0 ~ /\/>/) emit_pending(); next }
/<VALUE name="pluginList-arm">/ { inlist = 1; print; next }
inlist && /<KNOWNPLUGINS\/>/ {
    if (mode == "add") {
        ind = $0; sub(/<.*/, "", ind)
        print ind "<KNOWNPLUGINS>"; print ind "  " entry; print ind "</KNOWNPLUGINS>"; added = 1
    } else print
    inlist = 0; next
}
inlist && /<\/KNOWNPLUGINS>/ {
    if (mode == "add" && !added) { ind = $0; sub(/<.*/, "", ind); print ind "  " entry; added = 1 }
    inlist = 0; print; next
}
/<\/PROPERTIES>/ {
    if (mode == "add" && !added) {
        print "  <VALUE name=\"pluginList-arm\">"; print "    <KNOWNPLUGINS>"; print "      " entry
        print "    </KNOWNPLUGINS>"; print "  </VALUE>"; added = 1
    }
    print; next
}
{ print }
