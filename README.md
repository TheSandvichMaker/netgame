# netgame
I wrote this client and server over the course of about two weeks because I wanted to learn a little bit about network programming, something which I had no prior experience with whatsoever.
The netcode was written using winsock, after following [beej's networking guide](https://beej.us/guide/bgnet/).

The game is completely server-authoritative, following the design of Quake in that clients only send inputs to the server and receive entities to render. This is a very simple and robust scheme, however it suffers heavily from latency because all actions of the client need to go through the server and back before their results can be seen. I chose this model for its simplicity to get started, not because it's a good way to write a networked game.

The codebase was written in straightforward C, and has been written deliberately simpler and more naive than I might write it normally, in the hope that it might be useful for other people to look at. For this reason the packets are more piggy than they need to be as well. Fixing that could be an exercise for the reader!

As is, the client is hardcoded to connect to localhost, the code has not been tested running across multiple devices or networks, but it has been tested using [clumsy](https://jagt.github.io/clumsy/) to verify that it is reasonably robust in the face of all the dangers of UDP.

To test, launch NetServer.exe and then as many instances of NetClient.exe as you want.
For easy debugging in Visual Studio, I recommend you go in the solution properties and set multiple startup projects like this:
![image](https://user-images.githubusercontent.com/49493579/191977210-70e373c7-cca1-4630-a508-0dba90692244.png)

# controls
- W or Up: Move up  
- A or Left: Move left  
- D or Right: Move right  
- S or Down: Move down  
- LMB: Shoot  
- K: Kill client  
   
