const moduleStatus = document.getElementById("module-status");
const fetchStatus = document.getElementById("fetch-status");
const actionButton = document.getElementById("action-button");
const buttonStatus = document.getElementById("button-status");

moduleStatus.textContent = "Module status: loaded from Assets.";

if (actionButton && buttonStatus) {
  actionButton.addEventListener("click", () => {
    buttonStatus.textContent = "Button status: action handled by Assets module.";
  });
}

async function loadMessage() {
  try {
    const response = await fetch("http://localhost:8889/Assets/message.json", {
      method: "GET",
      headers: {
        "Content-Type": "application/json",
      },
    });

    if (!response.ok) {
      throw new Error("HTTP request failed.");
    }

    const payload = await response.json();
    if (payload.message !== "Fetch status: cross-origin JSON loaded from Assets.") {
      throw new Error("Unexpected JSON message.");
    }

    fetchStatus.textContent = payload.message;
  }
  catch {
    fetchStatus.textContent = "Fetch status: failed to load cross-origin JSON.";
  }
}

loadMessage();
