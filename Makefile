.PHONY: all regression
.PHONY: clean test regression regression-expressions regression-all uninstall install build
.PHONY: vm-regression vm-regression-expressions

INSTALL ?= install -v
MKDIR ?= mkdir
BUILDDIR = _build

.DEFAULT_GOAL := build

all: build test

build:
	dune b src runtime runtime32 stdlib tutorial vm

install: all
	dune b @install --profile=release
	dune    install --profile=release

_build/default/Lama.install:
	dune b @install

uninstall: _build/default/Lama.install
	$(RM) -r `opam var share`/Lama
	dune uninstall


regression-all: regression regression-expressions

test: regression
regression:
	dune test regression stdlib/regression

regression-expressions:
	dune test regression_long

vm-regression:
	@./vm/regression/prepare_regression.sh

vm-regression-expressions:
	@./vm/regression/prepare_regression_long.sh

clean:
	@dune clean

