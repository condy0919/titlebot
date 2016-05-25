sources = Glob('*.cpp')
sources.extend(Glob('./utils/*.cpp'))

Program('titlebot', sources,
        LIBS = ['boost_system', 'pthread'],
        CPPFLAGS = '-Ofast -std=c++17 -Wall -Wextra -fdiagnostics-color=always')
