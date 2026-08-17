You will see three main folders: include, lib, and bin. 

You need to move their contents into your C:\\MinGW directory:

|Folder In|Target Path in C:\\MinGW|Files to Move|
|-|-|-|
|include\\GL|C:\\MinGW\\include\\GL|freeglut.h, freeglut\_std.h, freeglut\_ext.h, glut.h|
|lib|C:\\MinGW\\lib|libfreeglut.a, libfreeglut\_static.a|
|bin|C:\\Windows\\System32|freeglut.dll (or keep it in your project folder)|



