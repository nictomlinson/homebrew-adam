# Microcode Architecture

## Microcode Encoding

|Type|encoding|description|
|---|---|---|
|immediate|1oooovvvvvvvvddd| <ul><li>1 identifies the microcode command as an Immediate command </li><li>oooo four bit destination option</li><li>vvvvvvvv 8 bit immediate value</li><li>ddd 3 bit destination port identifier</li></ul>|
|port|0oooobsssssddddd| <ul><li>0 identifies the microcode command as source Port command</li><li>oooo four bit desintation option</li><li>b one bit conditional branch indicator</li><li>sssss 5 bit source port identifier</li><li>ddddd 5 bit destination port identifier</li></ul>|
