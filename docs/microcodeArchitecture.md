# Microcode Architecture

## Microcode Encoding

|Type|encoding|description|
|---|---|---|
|immediate|1DDDDdddvvvvvvvv| 1 identifies the microcode command as an Immediate format<br>DDDD four bit destination option<br>ddd 3 bit destination port identifier<br>vvvvvvvv 8 bit immediate value|
|port|0DDDDdddtSSSSsss| 0 identifies the microcode command as source port command<br>DDDD four bit destination option<br>ddd 3 bit destination port identifier<br>t one bit conditional branch indicator, 1=> conditional, 0=>unconditional<br>SSSS four bit source port option<br>sss 3 bit source port identifier|
