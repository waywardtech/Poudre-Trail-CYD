async function loadGallery() {
  const res = await fetch("gallery_manifest.json");
  const runs = await res.json();

  runs.sort((a,b)=>b.votes-a.votes);

  const div = document.getElementById("gallery");

  runs.forEach(run => {
    const card = document.createElement("div");

    card.innerHTML = `
      <h3>${run.title}</h3>
      <p>${run.summary}</p>
      <p><em>${run.epilogue_excerpt}</em></p>
      <button onclick="viewRun('${run.run_url}')">View</button>
    `;

    div.appendChild(card);
  });
}

function viewRun(url) {
  window.location.href = `index.html?run=${url}`;
}

loadGallery();