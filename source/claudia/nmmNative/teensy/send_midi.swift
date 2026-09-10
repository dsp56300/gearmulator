// Send one explicitly selected test message to the unique Teensy MIDI port.
// macOS-only host utility; it does not broadcast to other MIDI destinations.
import Foundation
import CoreMIDI

func fail(_ message: String) -> Never {
    fputs(message + "\n", stderr)
    exit(1)
}
func check(_ status: OSStatus, _ operation: String) {
    if status != noErr { fail("\(operation) failed: \(status)") }
}
let args = CommandLine.arguments
if args.count != 4 { fail("usage: send_midi on|off NOTE VELOCITY") }
guard let note = UInt8(args[2]), note < 128,
      let velocity = UInt8(args[3]), velocity < 128,
      args[1] == "on" || args[1] == "off" else { fail("Invalid MIDI test message") }
var endpoints: [(MIDIEndpointRef, String)] = []
for index in 0..<MIDIGetNumberOfDestinations() {
    let endpoint = MIDIGetDestination(index)
    var unmanaged: Unmanaged<CFString>?
    if MIDIObjectGetStringProperty(endpoint, kMIDIPropertyDisplayName, &unmanaged) == noErr,
       let name = unmanaged?.takeRetainedValue() as String?,
       name.lowercased().contains("teensy") {
        endpoints.append((endpoint,name))
    }
}
guard endpoints.count == 1 else { fail("Expected exactly one Teensy MIDI destination; found \(endpoints.map{$0.1})") }
var client = MIDIClientRef()
var port = MIDIPortRef()
check(MIDIClientCreate("Native 101 test" as CFString,nil,nil,&client),"MIDIClientCreate")
defer { MIDIClientDispose(client) }
check(MIDIOutputPortCreate(client,"Native 101 output" as CFString,&port),"MIDIOutputPortCreate")
defer { MIDIPortDispose(port) }
let storage = UnsafeMutableRawPointer.allocate(byteCount: 1024, alignment: 8)
defer { storage.deallocate() }
let list = storage.bindMemory(to: MIDIPacketList.self,capacity: 1)
let packet = MIDIPacketListInit(list)
let bytes: [UInt8] = [args[1] == "on" ? 0x90 : 0x80, note, velocity]
_ = bytes.withUnsafeBufferPointer {
    MIDIPacketListAdd(list,1024,packet,0,bytes.count,$0.baseAddress!)
}
check(MIDISend(port,endpoints[0].0,list),"MIDISend")
print("Sent \(args[1]) note=\(note) velocity=\(velocity) to \(endpoints[0].1)")
