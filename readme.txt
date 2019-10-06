Solution "engine" generates executable that plays games; it includes UCI protocol interface. For that solution to compile, in file chess.h ENGINE has to be defined as 1.

Solution "chess" generates executable used for development work; it includes GUI with benchmarking and training functions. For that solution to compile, in file chess.h ENGINE has to be defined as 0.

Fizbo 2 was compiled with MS VC 2017 on Windows 8.