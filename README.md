# FujiNet SAM AI Chatbot v3

This is a simple interface using the OpenAI API to create a chatbot that runs on Atari 8-Bit computers using FujiNet. If using real FujiNet hardware the SAM emulation in FujiNet will also speak the response. This new version adds a small context window so the chat bot has a little history of the conversation. In addition, SAM has limited search capability to get up to date information when needed.

# Build App

1. Download latest [fujinet-lib](https://github.com/FujiNetWIFI/fujinet-lib/releases) for Atari and place the contents in `fujinet-ai-sam/fujinet-lib`
2. Make you have cc65 installed
3. Modify `src/config.h` with your proxy URL and default token
4. run `make` to compile the program

# Server

The server is a PHP script that uses a SQL database to store a short history of the chats based on a unique token and forwards OpenAI API requests to and from the app. Without the server middle man, the AI chatbot has no context of previous chats with the user and makes the conversation quite boring.

When the user runs the app client, it first checks if there is a FujiNet appkey containing a token. If not, it uses the default token to request a unique token from the server. If the default token matches on the server, it will return a unique token to be used by the app which is stored in the FujiNet appkey. This token is sent with every request and returned with every response.

The server stores the users textual chat request in the database along with the chatbot response. The FujiNet.online server is set to save the last 9 request and responses. At any time in the app, a user can type the `NEW` command to tell the server to wipe all record of the chat with that token id and the server will respond with a new token.

Older versions of AI SAM left the https connection open while the model created it's response which would sometimes timout. This version introduces polling to get the chatbot response. The server stores the user request and gives the client a unique ID that is used to check if a response is ready yet. While the client waits from a response, it displays "Thinking.." with periods added every few seconds to show it's still working. When the request is complete, the server will reply with the chatbot response.

If the ChatGPT model does not have enough information to reply to the user, it has the option to use a GPT search model (maximum 2 searches for each user chat request) to build a better response. This allows up to date information for questions like "Will it rain tomorrow?"

The server was tested on Ubuntu Jammy with Apache 2.4, PHP 8.2 and MySQL 8.0. You will need to edit the variables in `include.php` with your credentials for the server and OpenAI API.

# JSON API

The AI-SAM API communicates entirely through simple JSON requests and responses.
There are two endpoints:

* **POST** `/ai-sam/submit_request.php` — create tokens, send messages
* **GET**  `/ai-sam/check_request.php` — poll for completion of an AI response

Below are complete examples matching the actual server-side code.

---

## 1. Creating a New Conversation (Requesting a Token)

A new token is created when the client sends the **default token** as both `token_id` and `new`.

### **POST /ai-sam/submit_request.php**

**Request**

```json
{
  "token_id": "LeFakeToken",
  "new": "LeFakeToken"
}
```

**Successful Response**

```json
{
  "token_id": "4f3b2a1c9d8e76ab4c1f23de89ab0123"
}
```

The server inserts the new token into the database.
If an old token was provided (and `"new"` matches the default key), that old token and its messages are deleted.

---

## 2. Sending a Message

Client sends text to the server using an existing token.

### **POST /ai-sam/submit_request.php**

**Request**

```json
{
  "token_id": "4f3b2a1c9d8e76ab4c1f23de89ab0123",
  "message": "How do I mount an ATR image with FujiNet?"
}
```

**Successful Response**

```json
{
  "token_id": "4f3b2a1c9d8e76ab4c1f23de89ab0123",
  "message_id": 1234,
  "status": "pending"
}
```

The server:

* Stores the user message
* Creates a placeholder assistant message (status = 1)
* Returns its `message_id`
* Launches `process_request.php message_id &` in the background

---

## 3. Error Responses for `submit_request.php`

**Invalid JSON**

```json
{
  "error": "Invalid JSON input"
}
```

**Missing token**

```json
{
  "error": "Missing token_id"
}
```

**Invalid token**

```json
{
  "token_id": "4f3b2a1c9d8e76ab4c1f23de89ab0123",
  "error": "Invalid token"
}
```

**Missing message**

```json
{
  "token_id": "4f3b2a1c9d8e76ab4c1f23de89ab0123",
  "error": "Missing message"
}
```

**Message becomes empty after cleaning**

```json
{
  "token_id": "4f3b2a1c9d8e76ab4c1f23de89ab0123",
  "error": "Content is empty or contains only invalid characters"
}
```

---

## 4. Polling for Completion

Once a message is submitted, the client polls to see whether the assistant response is ready.

### **GET /ai-sam/check_request.php?token_id=TOKEN&message_id=ID**

**Example Request**

```
GET /ai-sam/check_request.php?token_id=4f3b2a1c9d8e76ab4c1f23de89ab0123&message_id=1234
```

### Pending Response

```json
{
  "token_id": "4f3b2a1c9d8e76ab4c1f23de89ab0123",
  "status": "pending"
}
```

### Completed Response (normal JSON content)

```json
{
  "token_id": "4f3b2a1c9d8e76ab4c1f23de89ab0123",
  "status": "complete",
  "text_display": "Here is how to mount an ATR: use the FujiNet CONFIG menu, pick a disk slot, and select your ATR file.",
  "text_sam": "Here is how to mount an A-T-R: use the Foo-gee-Net config menu, pick a disk slot, and select your A-T-R file."
}
```

`text_display` and `text_sam` come from the JSON written by `process_request.php`, converted to ATASCII and trimmed to 960 chars.

---

## 5. Error Responses for `check_request.php`

**Missing parameters**

```json
{
  "error": "Missing required parameters"
}
```

**Invalid token**

```json
{
  "token_id": "4f3b2a1c9d8e76ab4c1f23de89ab0123",
  "error": "Invalid token"
}
```

**Message does not belong to this token**

```json
{
  "token_id": "4f3b2a1c9d8e76ab4c1f23de89ab0123",
  "error": "Message not found or does not belong to this token"
}
```

---

