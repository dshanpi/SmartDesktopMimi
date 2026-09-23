import fs from 'fs';
import path from 'path';

const map = {
  'ExclamationTriangleIcon': 'LuAlertTriangle',
  'TrashIcon': 'LuTrash2',
  'ArrowLeftEndOnRectangleIcon': 'LuLogOut',
  'ChevronDownIcon': 'LuChevronDown',
  'CheckIcon': 'LuCheck',
  'LockClosedIcon': 'LuLock',
  'ExclamationCircleIcon': 'LuAlertCircle',
  'PlusCircleIcon': 'LuPlusCircle',
  'CheckCircleIcon': 'LuCheckCircle',
  'XCircleIcon': 'LuXCircle'
};

function walk(dir) {
  const files = fs.readdirSync(dir);
  for (const file of files) {
    const fullPath = path.join(dir, file);
    if (fs.statSync(fullPath).isDirectory()) {
      walk(fullPath);
    } else if (fullPath.endsWith('.tsx') || fullPath.endsWith('.ts')) {
      let content = fs.readFileSync(fullPath, 'utf-8');
      if (content.includes('@heroicons/react')) {
        console.log('Processing', fullPath);
        
        // Find the imported icons
        const importRegex = /import\s+\{([^}]+)\}\s+from\s+['"]@heroicons\/react[^'"]+['"];?/g;
        let match;
        const iconsToImport = new Set();
        
        while ((match = importRegex.exec(content)) !== null) {
          const icons = match[1].split(',').map(i => i.trim());
          for (const icon of icons) {
            if (map[icon]) {
              iconsToImport.add(map[icon]);
            }
          }
        }
        
        // Remove all heroicons imports
        content = content.replace(/import\s+\{[^}]+\}\s+from\s+['"]@heroicons\/react[^'"]+['"];?\n?/g, '');
        
        // Add lucide-react import
        if (iconsToImport.size > 0) {
          const lucideImport = `import { ${Array.from(iconsToImport).join(', ')} } from "lucide-react";\n`;
          // Find the last import statement or put at the top
          content = lucideImport + content;
          
          // Replace usages in the code
          for (const [hero, lucide] of Object.entries(map)) {
            const usageRegex = new RegExp(`\\b${hero}\\b`, 'g');
            content = content.replace(usageRegex, lucide);
          }
          
          fs.writeFileSync(fullPath, content);
        }
      }
    }
  }
}

walk('./src');
