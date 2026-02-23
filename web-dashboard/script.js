// Dummy values for testing UI
document.getElementById("water").innerText = "Normal";
document.getElementById("blockage").innerText = "No Blockage";
document.getElementById("motor").innerText = "OFF";

const alertList = document.getElementById("alertList");
function addAlert(msg) {
  const li = document.createElement("li");
  li.textContent = new Date().toLocaleTimeString() + " - " + msg;
  alertList.appendChild(li);
}
addAlert("System Initialized");
