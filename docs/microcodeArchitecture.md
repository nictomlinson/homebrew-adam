# Microcode Architecture

## Microcode Encoding

|Type|encoding|description|
|---|---|---|
|immediate|1oooovvvvvvvvddd| 1 identifies the microcode command as an Immediate format<br>oooo four bit destination option<br>vvvvvvvv 8 bit immediate value<br>ddd 3 bit destination port identifier|
|port|0oooobsssssddddd| 0 identifies the microcode command as source port command<br>oooo four bit desintation option<br>b one bit conditional branch indicator<br>sssss 5 bit source port identifier<br>ddddd 5 bit destination port identifier|
