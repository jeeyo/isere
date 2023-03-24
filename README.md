Limitations
-----------

- No Keep-Alive support
- 1024 byte limit on each header value (please be reminded that this affects your cookie size)

Simple Guide
------------

```sh
# install dependencies
brew install make
git clone <this f***ing repo>
git submodule update --init

# compile js example
cd examples/
make -j

# compile isere
cd ..
make -j

# run isere
./isere
```

try to access `http://localhost:8080/` and see the process logs  
  
feel free to try modify `examples/echo.esm.js` (don't forget to recompile it)
