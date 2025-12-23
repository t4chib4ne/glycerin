# glycerin

Glycerin is a simple utility for logging a single application.

## Notice

This software is in **alpha** status. I am currently using it daily but if you need a more battle tested simple logging utility please consider runit's [svlogd](https://smarden.org/runit/svlogd.8). This project was created for my own educational purposes and aims to be as simple as possible for easy maintainence. It is public for easier access on my systems.

## Why?

Previously I had been using runit to supervise my user daemons. This works flawlessly but I disliked that runit writes a bunch of pipes and devices to the service directory.
I wanted to switch to [nitro](https://github.com/leahneukirchen/nitro) but that project does not include a logger. So glycerin was born.

(glycerin is in no way affiliated with the nitro project)

## Installation

If you are on Gentoo you can use [tachibane-overlay](https://github.com/t4chib4ne/tachibane-overlay):

```shell
# eselect repository add tachibane git https://github.com/t4chib4ne/tachibane-overlay
# emaint sync -r tachibane
# emerge --ask sys-process/glycerin::tachibane
```

For any other distro you can simply clone this repo, change to a specific tag and run the following:

```shell
$ make
```

If you wish you can move `glycerin` to a directory on you `$PATH`.

## Features

- no external depencies
- no memory allocations during runtime
- rotate logs based on time and/or file size
- no worrying about where logs will be stored
- easy usage with nitro

## Usage

Please see the output of:

```shell
$ glycerin -h
Usage: glycerin [OPTIONS] APPNAME

Glycerin is a simple utility for logging a single application.

Options:
  -a u64 : maximum log file age in seconds
  -b u64 : size of the stdin buffer in bytes
  -d DIR : path for storing log files
  -f     : do not store log files in a subdirecotry
  -n u64 : maximum number of log files to keep
  -s u64 : maximum log file size in bytes
  -t     : cycle through timestamp formats for every line
```

Or read the man-page.

## Usage with nitro

Please take a look at the [nitro README](https://github.com/leahneukirchen/nitro/blob/master/README.md) first.

glycerin is designed to be used as a parametrized service. Create a `glycerin@` and either symlink the glycerin binary to `run` (if you are OK with the defaults) or create a `run` like so:

```bash
#!/bin/bash

exec glycerin <your options go here> $@
```

You can then create an instance of glycerin by creating as symlink in the service directory: `glycerin@my-app -> glycerin@`. In the `my-app` service create `log -> ../glycerin@my-app`. When creating the symlink for logging you might have to drop the `-r` flag of `ln` (I could not get it to work on my system otherwise).

## Contributing

All forms of contribution are welcome! Please be aware that if your contribution contains a new feature it may not be merged to keep the project simple. Reach out in an issue before opening a PR!
