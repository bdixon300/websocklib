.PHONY: image build test shell clean fclean

# Build the Docker image (base layer only — deps/source mounted as volume)
image:
	docker compose build

# Compile the library and tests inside the container
build: image
	docker compose run --rm build

# Build and run all tests
test: image
	docker compose run --rm test

# Drop into an interactive shell with the full build environment
shell: image
	docker compose run --rm shell

# Remove local build artefacts (Conan cache volume is preserved)
clean:
	rm -rf build/

# Remove build artefacts AND the Conan cache Docker volume
fclean: clean
	docker compose down --volumes --rmi local
