# FujiNet SAM AI Chatbot v2

This is a simple interface using the OpenAI API to create a chatbot that runs on Atari 8-Bit computers using FujiNet. If using real FujiNet hardware the SAM emulation in FujiNet will also speak the response. This new version adds a small context window so the chat bot has a little history of the conversation.

# Build App

1. Download latest [fujinet-lib](https://github.com/FujiNetWIFI/fujinet-lib/releases) for Atari and place the contents in `fujinet-ai-sam/fujinet-lib`
2. Make you have cc65 installed
3. Modify `src/config.h` with your proxy URL and default token
4. run `make` to compile the program

# Proxy Server

The proxy server is a PHP script that uses a SQL database to store a short history of the chats based on a unique token and forwards OpenAI API requests to and from the app. Without the server middle man, the AI chatbot has no context of previous chats with the user and makes the conversation quite boring.

When the user runs the app, it first checks if there is a FujiNet appkey containing a token. If not, it uses the set default token to request a token from the proxy server. If the default token matches on the proxy server, it will return a unique token to be used by the app which is stored in the appkey. This token is sent with every request.

The proxy server stores the users textual chat request in a database along with the chatbot response. The FujiNet.online server is set to save the last 9 request and responses. At any time in the app, a user can type the `new` command to tell the server to wipe all record of the chat with that token id and the server will respond with a new token.

The proxy server was tested on Ubuntu Jammy with Apache 2.4, PHP 8.2 and MySQL 8.0. You will need to edit the variables at the top of the script with your credentials for the server and OpenAI API.
