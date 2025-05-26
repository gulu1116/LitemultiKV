

const net = require('net');

const client = net.createConnection({ port: 8000, host: '192.168.127.141' }, () => {
  console.log('connect kvstore');

  client.write('GET Teacher');
});

client.on('data', (data) => {
  console.log(`recv：${data.toString()}`);

  client.end();
});

client.on('error', (err) => {
  console.error('connect failed：', err);
});

client.on('close', () => {
  console.log('close connection');
});



