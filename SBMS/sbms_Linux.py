
#
# Put Linux-specific configurations here
#

def InitENV(env):
	env.PrependUnique(LINKFLAGS = ['-Wl,-E'])
	env.AppendUnique(LIBS = ['numa'])


