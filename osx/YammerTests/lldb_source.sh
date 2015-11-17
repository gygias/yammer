command script import lldb.macosx.heap

command process handle -n true -p true -s false SIGPIPE

command alias p print
command alias mh malloc_info --stack-history
