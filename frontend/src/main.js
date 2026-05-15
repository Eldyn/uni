import { mount } from 'svelte'
import './app.css'
import App from './App.svelte'

const app = mount(App, {
  target: document.getElementById('app') ?? document.body, 
  // ?? document.body Risolve l'errore usando il body come fallback se #app è null - anche se ora esiste il div
})

export default app
