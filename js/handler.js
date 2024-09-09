var http = require('http');

http.createServer(function (req, res) {
  res.end(JSON.stringify({ k: 'v' }));
}).listen(8080);
