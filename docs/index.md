# Adam

Adam, it's not an acronym. My wife named it because it is the first homebrew 
computer I have built and I suspect the last.
The design first started in about 2019 and inspiration came from [Ben Eaters]
excellent [8-bit breadboard computer] videos. The other major influence is from
[Bill Buzbee's] [Magic-1] homebrew CPU.

Adam is a homebrew, 16-bit, microcoded, transport triggered architecture CPU and
computer. It was not at first clear that the outline design was a transport
triggered architecture, or that such an architecture had a name, until [this
article] was stumbled upon.

By homebrew it is meant that it is not based on some pre-existing cpu or
computer design, it will be built from mainly 74' series logic chips with no
microcontrollers or other complex components. Liberal use of SRAM is expected.

The computer will comprise a set of modules connected by a common [bus]. Each
module will be built using 102mmx102mm PCBs. This size is chosen as it is the
maximum size for the highly discounted pcb manufacture by [JLC PCB]; there is a
jump in the price above this size. Also, the 102mm side gives enough room for a
2x40 2.54mm pitch pin header. The modules will be connected to a backplane,
itself comprising a set of 102mm square pcbs each with a number of 2x40 headers.

The CPU is microcoded with a 16 bit microcode instructions. Instructions are
generally of the form `dst = src` where `dst` identifies a destination port and
`src` identifies a source port. There are two formats for the instruction. Port
format instructions have 5 bites each for the allow the source and destination
ports allowing for 32 distinct ports. In addition, Port instructions include a
conditional branch bit that will cause a conditional branch if a condition is
met. Immediate format instructions allow an immediate 8-bit value encoded in the
instruction as the source value but are limited to only the lower 8 destination
ports identified with 3 bits. In both cases, a 4 bit destination option is also
provided that the destination port can use to control the action performed.

See [microcode architecture] for further details about the microcode.

The microcode implements higher-level ISA. A stretch goal is for the microcode
to be editable from running code. The microcode is served by the [Microcode]
module.

[Ben Eaters]: https://www.youtube.com/c/beneater
[8-bit breadboard computer]: https://www.youtube.com/playlist?list=PLowKtXNTBypGqImE405J2565dvjafglHU
[Bill Buzbee's]: http://www.homebrewcpu.com/about_me.htm
[Magic-1]: http://www.homebrewcpu.com/
[this article]: https://hackaday.com/2017/04/21/an-8-bit-transport-triggered-architecture-cpu-in-ttl/
[bus]: ./interconnectDesign.md
[JLC PCB]: https://jlcpcb.com/
[Microcode]: ./modules/microcode.md
[microcode architecture]: ./microcodeArchitecture.md