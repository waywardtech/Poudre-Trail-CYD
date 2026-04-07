async function loadRun() {
  const params = new URLSearchParams(window.location.search);
  const runFile = params.get("run");
  if (!runFile) return;

  const res = await fetch(runFile);
  const data = await res.json();

  const div = document.getElementById("content");
  let html = "<h2>Journey</h2>";

  data.log.forEach(line => {
    html += "<p>" + line + "</p>";
  });

  html += "<h2>Epilogue</h2>";
  html += "<p>" + data.epilogue + "</p>";

  div.innerHTML = html;
}

loadRun();