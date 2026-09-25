const header = document.querySelector("[data-header]");
const menuButton = document.querySelector("[data-menu-toggle]");
const navigation = document.querySelector("[data-nav]");

const updateHeader = () => {
  header?.classList.toggle("scrolled", window.scrollY > 12);
};

const closeMenu = () => {
  navigation?.classList.remove("open");
  menuButton?.setAttribute("aria-expanded", "false");
  menuButton?.setAttribute("aria-label", "打开导航");
  document.body.classList.remove("menu-open");
};

updateHeader();
window.addEventListener("scroll", updateHeader, { passive: true });

menuButton?.addEventListener("click", () => {
  const isOpening = menuButton.getAttribute("aria-expanded") !== "true";
  menuButton.setAttribute("aria-expanded", String(isOpening));
  menuButton.setAttribute("aria-label", isOpening ? "关闭导航" : "打开导航");
  navigation?.classList.toggle("open", isOpening);
  document.body.classList.toggle("menu-open", isOpening);
});

navigation
  ?.querySelectorAll("a")
  .forEach((link) => link.addEventListener("click", closeMenu));

document.addEventListener("keydown", (event) => {
  if (event.key !== "Escape" || menuButton?.getAttribute("aria-expanded") !== "true") {
    return;
  }

  closeMenu();
  menuButton.focus();
});

window.addEventListener("resize", () => {
  if (window.innerWidth > 800) closeMenu();
});

const tabs = Array.from(document.querySelectorAll("[data-evidence-tab]"));
const panels = Array.from(document.querySelectorAll("[data-evidence-panel]"));

const activateEvidence = (selectedTab, moveFocus = false) => {
  const selectedValue = selectedTab.dataset.evidenceTab;

  tabs.forEach((tab) => {
    const isSelected = tab === selectedTab;
    tab.classList.toggle("is-active", isSelected);
    tab.setAttribute("aria-selected", String(isSelected));
    tab.tabIndex = isSelected ? 0 : -1;
  });

  panels.forEach((panel) => {
    const isSelected = panel.dataset.evidencePanel === selectedValue;
    panel.classList.toggle("is-active", isSelected);
    panel.hidden = !isSelected;
  });

  if (moveFocus) selectedTab.focus();
};

if (tabs.length > 0) {
  activateEvidence(tabs.find((tab) => tab.classList.contains("is-active")) ?? tabs[0]);

  tabs.forEach((tab, index) => {
    tab.addEventListener("click", () => activateEvidence(tab));
    tab.addEventListener("keydown", (event) => {
      if (!["ArrowRight", "ArrowDown", "ArrowLeft", "ArrowUp", "Home", "End"].includes(event.key)) {
        return;
      }

      event.preventDefault();
      let nextIndex = index;
      if (["ArrowRight", "ArrowDown"].includes(event.key)) nextIndex = (index + 1) % tabs.length;
      if (["ArrowLeft", "ArrowUp"].includes(event.key)) nextIndex = (index - 1 + tabs.length) % tabs.length;
      if (event.key === "Home") nextIndex = 0;
      if (event.key === "End") nextIndex = tabs.length - 1;
      activateEvidence(tabs[nextIndex], true);
    });
  });
}
