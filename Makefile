BUILD ?= build
PREFIX ?= /usr/local

.PHONY: build test run demo clean install

build:
	cmake -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD) --parallel

test: build
	ctest --test-dir $(BUILD) --output-on-failure

run: build
	./$(BUILD)/strata serve

demo: build
	rm -rf /tmp/strata-demo /tmp/strata-demo.db
	./examples/demo/make-demo-repo.sh /tmp/strata-demo
	./$(BUILD)/strata ingest /tmp/strata-demo --db /tmp/strata-demo.db
	./$(BUILD)/strata serve --db /tmp/strata-demo.db

install: build
	cmake --install $(BUILD) --prefix $(PREFIX)

clean:
	rm -rf $(BUILD)
